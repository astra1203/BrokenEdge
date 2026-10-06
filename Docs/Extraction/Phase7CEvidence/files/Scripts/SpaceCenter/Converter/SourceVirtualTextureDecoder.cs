using System.IO.Compression;
using System.Reflection;
using CUE4Parse.UE4.Assets.Exports.Texture;
using CUE4Parse_Conversion.Textures;

// The pinned generic decoder visits Morton-address padding in rectangular legacy VT
// images. Decode only source image tiles, validating every compressed tile and row.
public static class SourceVirtualTextureDecoder
{
    public static CTexture Decode(UTexture texture, int mip)
    {
        var vt = texture.PlatformData.VTData ?? throw new InvalidDataException("Not a virtual texture");
        if (!vt.IsLegacyData() || vt.NumLayers != 1) throw new NotSupportedException("Expected one-layer legacy VT");
        int width = Math.Max(1, (int)vt.Width >> mip), height = Math.Max(1, (int)vt.Height >> mip);
        int tileSize = (int)vt.TileSize, physical = (int)vt.GetPhysicalTileSize(), border = (int)vt.TileBorderSize;
        var format = PixelFormatUtils.PixelFormats[vt.LayerTypes[0]];
        int packedSize = (physical + format.BlockSizeX - 1) / format.BlockSizeX *
                         ((physical + format.BlockSizeY - 1) / format.BlockSizeY) * format.BlockBytes;
        var decode = typeof(TextureDecoder).GetMethod("DecodeBytes", BindingFlags.Static | BindingFlags.NonPublic)
                     ?? throw new MissingMethodException("Pinned CUE4Parse DecodeBytes changed");
        byte[]? result = null;
        EPixelFormat pixelFormat = EPixelFormat.PF_Unknown;
        for (int y = 0; y < height; y += tileSize)
        for (int x = 0; x < width; x += tileSize)
        {
            uint address = Morton((uint)(x / tileSize)) | (Morton((uint)(y / tileSize)) << 1);
            if (!vt.IsValidAddress(mip, address)) throw new InvalidDataException("Missing in-bounds source tile");
            var (chunkIndex, offset, length) = vt.GetTileData(mip, address, 0);
            if (length == 0) throw new InvalidDataException("Empty in-bounds source tile");
            var chunk = vt.Chunks[chunkIndex];
            byte[] packed;
            if (chunk.CodecType[0] == EVirtualTextureCodec.ZippedGPU_DEPRECATED)
            {
                using var source = new MemoryStream(chunk.BulkData.Data!, (int)offset, (int)length, false);
                using var zip = new ZLibStream(source, CompressionMode.Decompress);
                packed = new byte[packedSize];
                zip.ReadExactly(packed);
                if (zip.ReadByte() != -1) throw new InvalidDataException("Oversized compressed source tile");
            }
            else if (chunk.CodecType[0] == EVirtualTextureCodec.RawGPU)
            {
                if (length != packedSize) throw new InvalidDataException("Raw tile size differs");
                packed = chunk.BulkData.Data!.AsSpan((int)offset, packedSize).ToArray();
            }
            else throw new NotSupportedException("Unsupported source codec: " + chunk.CodecType[0]);
            object?[] arguments = [packed, physical, physical, 1, format, texture.IsNormalMap, null, EPixelFormat.PF_Unknown];
            decode.Invoke(null, arguments);
            var pixels = (byte[])arguments[6]!;
            var tileFormat = (EPixelFormat)arguments[7]!;
            int stride = pixels.Length / (physical * physical);
            if (result == null) { result = new byte[width * height * stride]; pixelFormat = tileFormat; }
            if (pixelFormat != tileFormat) throw new InvalidDataException("Mixed tile pixel formats");
            int rows = Math.Min(tileSize, height - y), columns = Math.Min(tileSize, width - x);
            for (int row = 0; row < rows; row++)
                Buffer.BlockCopy(pixels, ((row + border) * physical + border) * stride,
                    result, ((y + row) * width + x) * stride, columns * stride);
        }
        return new CTexture(width, height, pixelFormat, result ?? throw new InvalidDataException("Empty texture"));
    }

    private static uint Morton(uint value)
    {
        value &= 0x0000ffff;
        value = (value | value << 8) & 0x00ff00ff;
        value = (value | value << 4) & 0x0f0f0f0f;
        value = (value | value << 2) & 0x33333333;
        return (value | value << 1) & 0x55555555;
    }
}
