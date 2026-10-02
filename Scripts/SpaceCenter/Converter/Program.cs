using CUE4Parse;
using CUE4Parse.FileProvider;
using CUE4Parse.UE4.Assets.Exports.StaticMesh;
using CUE4Parse.UE4.Assets.Exports.Texture;
using CUE4Parse.UE4.Assets.Exports.Component;
using CUE4Parse.UE4.Assets.Exports.Component.StaticMesh;
using CUE4Parse.UE4.Objects.Core.Math;
using CUE4Parse.UE4.Versions;
using CUE4Parse_Conversion;
using CUE4Parse_Conversion.Options;
using CUE4Parse_Conversion.Textures.BC;
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
Directory.CreateDirectory(output);
Log.Logger = new LoggerConfiguration().MinimumLevel.Warning().WriteTo.Console().CreateLogger();
CUE4ParseLog.UseLogger(Log.Logger);
using var provider = new DefaultFileProvider(source, SearchOption.AllDirectories,
    new VersionContainer(EGame.GAME_DeltaForce), StringComparer.OrdinalIgnoreCase);
provider.Initialize();
if (mode == "export")
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
        result["export_count"] = package.ExportMapLength;
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
                    objects.Add(record);
                }
                else if (mode == "layout" && obj is UStaticMeshComponent component)
                {
                    var mesh = component.GetStaticMesh();
                    var record = new JObject { ["export_index"] = i, ["name"] = obj.Name,
                        ["class"] = obj.ExportType, ["source_object"] = obj.GetPathName(),
                        ["mesh"] = JToken.FromObject(mesh), ["properties"] = JObject.FromObject(obj)["Properties"] };
                    // Compose each ancestor exactly once. Do not silently break cyclic attachments.
                    var transform = component.GetRelativeTransform();
                    var seen = new HashSet<USceneComponent> { component };
                    for (var parent = component.GetAttachParent(); parent != null; parent = parent.GetAttachParent())
                    {
                        if (!seen.Add(parent)) throw new InvalidDataException("Cyclic scene attachment");
                        transform *= parent.GetRelativeTransform();
                    }
                    record["transform"] = JToken.FromObject(transform);
                    if (component is UInstancedStaticMeshComponent instanced)
                        record["instances"] = instanced.PerInstanceSMData == null ? null : JToken.FromObject(instanced.PerInstanceSMData);
                    objects.Add(record);
                }
                else if (obj is UStaticMesh or UTexture2D)
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
                    }
                    session.Add(obj);
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
        meshFormat: EMeshFormat.Gltf2, exportMaterials: false));
    exportFailed = results.Count == 0 || results.Any(r => !r.Success);
    await File.WriteAllTextAsync(Path.Combine(output, "ExportResults.json"), JsonConvert.SerializeObject(
        results.Select(r => new { r.Success, r.ObjectPath, r.DiskFilePaths, error = r.Error?.ToString() }), Formatting.Indented));
}
await File.WriteAllTextAsync(Path.Combine(output, "Packages.json"), reports.ToString(Formatting.Indented));
return exportFailed || reports.Any(r => r["error"] != null || r["failures"] is JArray { Count: > 0 }) ? 1 : 0;
