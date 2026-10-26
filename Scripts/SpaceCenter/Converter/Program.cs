using CUE4Parse;
using CUE4Parse.FileProvider;
using CUE4Parse.FileProvider.Objects;
using CUE4Parse.UE4.Assets.Exports.StaticMesh;
using CUE4Parse.UE4.Assets.Exports.Texture;
using CUE4Parse.UE4.Assets.Exports.Component;
using CUE4Parse.UE4.Assets.Exports.Component.StaticMesh;
using CUE4Parse.UE4.Objects.Core.Math;
using CUE4Parse.UE4.Versions;
using CUE4Parse.UE4.Assets;
using CUE4Parse.UE4.Objects.UObject;
using CUE4Parse_Conversion;
using CUE4Parse_Conversion.Dto;
using CUE4Parse_Conversion.Options;
using CUE4Parse_Conversion.Textures.BC;
using CUE4Parse_Conversion.Textures;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using Serilog;

// File request keeps complete Unicode paths out of shell argument quoting.
if (args.Length != 1) throw new ArgumentException("Expected request.json");
var request = JObject.Parse(File.ReadAllText(args[0]));
var source = request.Value<string>("source")!;
var output = request.Value<string>("output")!;
var mode = request.Value<string>("mode") ?? "inspect";
var recovery = request["geometry_recovery"]?.Values<string>().ToHashSet(StringComparer.OrdinalIgnoreCase) ?? [];
var templates = request["component_templates"] as JObject;
ObjectTypeRegistry.RegisterClass("OpenWorldFoliageAsset", typeof(SourceFoliageProbe));
Directory.CreateDirectory(output);
Log.Logger = new LoggerConfiguration().MinimumLevel.Warning().WriteTo.Console().CreateLogger();
CUE4ParseLog.UseLogger(Log.Logger);
var sourceDirectory = new DirectoryInfo(source);
var engineDirectory = new DirectoryInfo(Path.Combine(sourceDirectory.Parent!.FullName, "Engine"));
using var provider = new DefaultFileProvider(sourceDirectory, SearchOption.AllDirectories,
    new VersionContainer(EGame.GAME_DeltaForce), StringComparer.OrdinalIgnoreCase);
provider.Initialize();
// This pinned provider resolves extra loose directories against the game root.
// Mount Engine with its own base directory so sibling paths do not contain ../.
if (engineDirectory.Exists)
{
    var engineFiles = engineDirectory.EnumerateFiles("*.*", SearchOption.AllDirectories)
        .Where(file => GameFile.UeKnownExtensionsSet.Contains(file.Extension.TrimStart('.').ToUpperInvariant()))
        .Select(file => new OsGameFile(engineDirectory, file, "Engine/", provider.Versions))
        .ToDictionary(file => file.Path, file => (GameFile)file, StringComparer.OrdinalIgnoreCase);
    provider.Files.AddFiles(engineFiles);
}
if (mode is "export" or "texture-mips")
{
    var dll = Path.Combine(AppContext.BaseDirectory, "Detex.dll");
    if (!DetexHelper.LoadDll(dll)) throw new IOException("Cannot extract bundled Detex decoder");
    DetexHelper.Initialize(dll);
}
var reports = new JArray();
var session = new ExportSession { MaxDegreeOfParallelism = 2 };
var exportFailed = false;
foreach (var path in request["packages"]!.Values<string>())
{
    var result = new JObject { ["package"] = path };
    reports.Add(result);
    try
    {
        var package = provider.LoadPackage(path!);
        if (mode == "layout" && templates?[path!] is JObject componentTemplates)
        {
            foreach (var mapping in componentTemplates.Properties())
            {
                var component = package.ExportsLazy[int.Parse(mapping.Name)].Value;
                var templatePackage = provider.LoadPackage(mapping.Value.Value<string>("package")!);
                var templateIndex = mapping.Value.Value<int>("export_index");
                var template = templatePackage.ExportsLazy[templateIndex].Value;
                if (component.Name + "_GEN_VARIABLE" != template.Name && component.Name != template.Name)
                    throw new InvalidDataException("Source construction-script template name differs");
                component.Template = templatePackage.ResolvePackageIndex(new FPackageIndex(templatePackage, templateIndex + 1));
            }
        }
        result["export_count"] = package.ExportMapLength;
        if (mode == "inspect")
        {
            result["filter_editor_only"] = package.HasFlags(EPackageFlags.PKG_FilterEditorOnly);
            result["cooked"] = package.HasFlags(EPackageFlags.PKG_Cooked);
            if (package is Package inspectedPackage)
                result["imports"] = JToken.FromObject(Enumerable.Range(0, inspectedPackage.ImportMap.Length)
                    .Select(index => new FPackageIndex(inspectedPackage, -index - 1)));
        }
        var objects = new JArray();
        var failures = new JArray();
        for (var i = 0; i < package.ExportsLazy.Length; i++)
        {
            try
            {
                var obj = package.ExportsLazy[i].Value;
                if (mode == "inspect")
                {
                    var record = JObject.FromObject(obj);
                    record["export_index"] = i;
                    record["class_reference"] = obj.Class == null ? null : JToken.FromObject(obj.Class);
                    if (obj is SourceFoliageProbe foliage)
                    {
                        var tailPath = Path.Combine(output, $"{obj.Name}.tail.bin");
                        File.WriteAllBytes(tailPath, foliage.Tail);
                        record["source_tail"] = new JObject { ["file"] = tailPath, ["offset"] = foliage.TailOffset,
                            ["length"] = foliage.Tail.Length };
                    }
                    if (package is Package legacyPackage)
                    {
                        var sourceExport = legacyPackage.ExportMap[i];
                        record["source_export"] = new JObject
                        {
                            ["serial_offset"] = sourceExport.SerialOffset,
                            ["serial_size"] = sourceExport.SerialSize,
                            ["class_index"] = sourceExport.ClassIndex.Index
                        };
                    }
                    foreach (var property in new[] { "Expressions", "FunctionExpressions" })
                    {
                        if (obj.TryGetValue(out FPackageIndex[] indices, property))
                            record[property + "SourceIndices"] = JToken.FromObject(indices.Select(index => index.Index));
                    }
                    objects.Add(record);
                }
                else if (mode == "layout" && obj is UStaticMeshComponent component)
                {
                    var mesh = component.GetStaticMesh();
                    var properties = new JObject();
                    if (obj.Template?.Load<CUE4Parse.UE4.Assets.Exports.UObject>() is { } componentTemplate)
                        properties = (JObject?)JObject.FromObject(componentTemplate)["Properties"] ?? new JObject();
                    properties.Merge(JObject.FromObject(obj)["Properties"], new JsonMergeSettings { MergeArrayHandling = MergeArrayHandling.Replace });
                    var record = new JObject { ["export_index"] = i, ["name"] = obj.Name,
                        ["class"] = obj.ExportType, ["source_object"] = obj.GetPathName(),
                        ["mesh"] = JToken.FromObject(mesh), ["properties"] = properties,
                        ["template"] = obj.Template == null ? null : JToken.FromObject(obj.Template) };
                    // Compose each ancestor exactly once. Do not silently break cyclic attachments.
                    var transform = SourceRelativeTransform(component);
                    var seen = new HashSet<USceneComponent> { component };
                    for (var parent = component.GetAttachParent(); parent != null; parent = parent.GetAttachParent())
                    {
                        if (!seen.Add(parent)) throw new InvalidDataException("Cyclic scene attachment");
                        transform *= SourceRelativeTransform(parent);
                    }
                    record["transform"] = JToken.FromObject(transform);
                    if (component is UInstancedStaticMeshComponent instanced)
                        record["instances"] = instanced.PerInstanceSMData == null ? null : JToken.FromObject(instanced.PerInstanceSMData);
                    objects.Add(record);
                }
                else if (obj is UStaticMesh or UTexture or CUE4Parse.UE4.Assets.Exports.Animation.UAnimSequence)
                {
                    var record = new JObject { ["name"] = obj.Name, ["class"] = obj.ExportType,
                        ["object_path"] = obj.GetPathName() };
                    if (obj is UStaticMesh staticMesh)
                    {
                        if (staticMesh.RenderData == null && recovery.Contains(path!))
                        {
                            var archiveName = package.Name + ".uexp";
                            if (!FStaticMeshRenderData.SpaceCenterGeometrySnapshots.TryGetValue(archiveName, out var captured))
                                throw new InvalidDataException("No geometry captured for recovery: " + archiveName);
                            var lod = captured.LODs!.First(l => !l.SkipLod);
                            var vertices = lod.PositionVertexBuffer!.Verts;
                            if (vertices.Length < 3 || vertices.Any(v => !float.IsFinite(v.X) || !float.IsFinite(v.Y) || !float.IsFinite(v.Z)))
                                throw new InvalidDataException("Recovered vertices invalid");
                            captured.Bounds = new FBoxSphereBounds(new FBox(
                                new FVector(vertices.Min(v => v.X), vertices.Min(v => v.Y), vertices.Min(v => v.Z)),
                                new FVector(vertices.Max(v => v.X), vertices.Max(v => v.Y), vertices.Max(v => v.Z))));
                            typeof(UStaticMesh).GetProperty("RenderData")!.SetValue(staticMesh, captured);
                            var materials = staticMesh.GetOrDefault("StaticMaterials", Array.Empty<FStaticMaterial>());
                            typeof(UStaticMesh).GetProperty("StaticMaterials")!.SetValue(staticMesh, materials);
                            typeof(UStaticMesh).GetProperty("Materials")!.SetValue(staticMesh, materials.Select(m => m.MaterialInterface).ToArray());
                            record["recovery"] = "Original LOD buffers; unsupported distance-field tail omitted; bounds recomputed; tagged materials retained";
                        }
                        record["materials"] = JToken.FromObject(staticMesh.StaticMaterials);
                        if (staticMesh.RenderData?.Bounds != null) record["bounds"] = JToken.FromObject(staticMesh.RenderData.Bounds);
                        if (mode == "mesh-slots")
                        {
                            using var meshDto = new StaticMeshDto(staticMesh, EMeshQuality.Highest);
                            record["export_lods"] = JToken.FromObject(meshDto.LODs.Select(lod => new
                            {
                                source_lod = lod.SourceLodIndex,
                                sections = lod.Sections.Select(section => new
                                {
                                    material_index = section.MaterialIndex,
                                    name = meshDto.GetMaterial(section)?.SlotName,
                                    first_index = section.FirstIndex,
                                    triangles = section.NumFaces
                                })
                            }));
                        }
                    }
                    if (obj is UTexture texture)
                    {
                        record["srgb"] = texture.SRGB;
                        record["compression"] = texture.CompressionSettings.ToString();
                        record["pixel_format"] = texture.Format.ToString();
                        record["width"] = texture.PlatformData.SizeX;
                        record["height"] = texture.PlatformData.SizeY;
                        record["properties"] = JObject.FromObject(obj)["Properties"];
                        if (mode == "rawtexture")
                        {
                            var mip = texture.GetFirstMip() ?? throw new InvalidDataException("Missing original mip");
                            var rawPath = Path.Combine(output, obj.Name + ".raw");
                            File.WriteAllBytes(rawPath, mip.BulkData.Data!);
                            record["raw_file"] = rawPath;
                            record["raw_width"] = mip.SizeX;
                            record["raw_height"] = mip.SizeY;
                        }
                        if (mode == "texture-mips")
                        {
                            var attempts = new JArray();
                            record["mip_attempts"] = attempts;
                            int count = texture.PlatformData.VTData?.NumMips is { } mipCount ? (int)mipCount : texture.PlatformData.Mips.Length;
                            for (int mipIndex = 0; mipIndex < count; mipIndex++)
                            {
                                var attempt = new JObject { ["mip"] = mipIndex };
                                attempts.Add(attempt);
                                try
                                {
                                    if (texture.PlatformData.VTData is { } virtualData)
                                    {
                                        var tile = virtualData.GetTileData(mipIndex, 0, 0);
                                        var chunk = virtualData.Chunks[tile.Item1];
                                        attempt["first_tile"] = new JObject { ["codec"] = chunk.CodecType[0].ToString(),
                                            ["offset"] = tile.Item2, ["length"] = tile.Item3,
                                            ["prefix"] = Convert.ToHexString(chunk.BulkData.Data!.AsSpan((int)tile.Item2, Math.Min(24, (int)tile.Item3))) };
                                    }
                                    var decoded = texture.PlatformData.VTData is { } sourceVt && sourceVt.IsLegacyData() && sourceVt.NumLayers == 1
                                        ? SourceVirtualTextureDecoder.Decode(texture, mipIndex)
                                        : texture.DecodeMip(mipIndex) ?? throw new InvalidDataException("Empty mip");
                                    if (mipIndex > 0 && texture.PlatformData.VTData is { } vtData)
                                    {
                                        // Legacy single-tile mip decode allocates the base extent. Only the
                                        // original mip rectangle contains pixels; omit unused allocation.
                                        int width = Math.Max(1, (int)vtData.Width >> mipIndex);
                                        int height = Math.Max(1, (int)vtData.Height >> mipIndex);
                                        if (width > decoded.Width || height > decoded.Height) throw new InvalidDataException("Decoded mip smaller than source extent");
                                        int stride = decoded.Data.Length / (decoded.Width * decoded.Height);
                                        if (stride is < 1 or > 16) throw new InvalidDataException("Unexpected decoded pixel size");
                                        var cropped = new byte[width * height * stride];
                                        for (int row = 0; row < height; row++)
                                            Buffer.BlockCopy(decoded.Data, row * decoded.Width * stride, cropped, row * width * stride, width * stride);
                                        decoded = new CTexture(width, height, decoded.PixelFormat, cropped);
                                    }
                                    var encoded = decoded.Encode(new ExportOptions(), out var extension);
                                    var file = Path.Combine(output, obj.Name + $"_MIP{mipIndex}." + extension);
                                    File.WriteAllBytes(file, encoded);
                                    attempt["file"] = file;
                                    attempt["width"] = decoded.Width;
                                    attempt["height"] = decoded.Height;
                                    break;
                                }
                                catch (Exception ex) { attempt["error"] = ex.Message; }
                            }
                        }
                    }
                    if (mode == "export") session.Add(obj);
                    objects.Add(record);
                }
            }
            catch (Exception ex) { failures.Add(new JObject { ["index"] = i, ["error"] = ex.ToString() }); }
        }
        result["objects"] = objects;
        result["failures"] = failures;
    }
    catch (Exception ex) { result["error"] = ex.ToString(); }
    Console.WriteLine($"PARSED {path}: {result["export_count"]} exports");
}
if (mode == "export")
{
    var results = await session.RunAsync(output, new ExportOptions(
        meshFormat: request.Value<string>("format") == "ActorX" ? EMeshFormat.ActorX : EMeshFormat.Gltf2, exportMaterials: false));
    exportFailed = results.Count == 0 || results.Any(r => !r.Success);
    await File.WriteAllTextAsync(Path.Combine(output, "ExportResults.json"), JsonConvert.SerializeObject(
        results.Select(r => new { r.Success, r.ObjectPath, r.DiskFilePaths, error = r.Error?.ToString() }), Formatting.Indented));
}
await File.WriteAllTextAsync(Path.Combine(output, "Packages.json"), reports.ToString(Formatting.Indented));
return exportFailed || reports.Any(r => r["error"] != null || r["failures"] is JArray { Count: > 0 }) ? 1 : 0;

static FTransform SourceRelativeTransform(USceneComponent component)
{
    FVector? location = null, scale = null;
    FRotator? rotation = null;
    var seen = new HashSet<USceneComponent>();
    for (var current = component; current != null; current = current.Template?.Load<USceneComponent>())
    {
        if (!seen.Add(current)) throw new InvalidDataException("Cyclic component template");
        if (location == null && current.TryGetValue(out FVector loc, "RelativeLocation")) location = loc;
        if (rotation == null && current.TryGetValue(out FRotator rot, "RelativeRotation")) rotation = rot;
        if (scale == null && current.TryGetValue(out FVector scl, "RelativeScale3D")) scale = scl;
    }
    return new FTransform(rotation ?? FRotator.ZeroRotator, location ?? FVector.ZeroVector, scale ?? FVector.OneVector);
}
