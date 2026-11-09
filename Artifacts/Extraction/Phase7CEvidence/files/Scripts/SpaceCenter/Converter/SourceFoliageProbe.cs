using CUE4Parse.UE4.Assets.Exports;
using CUE4Parse.UE4.Assets.Readers;

// Preserve unsupported source bytes. Decoding decisions are made explicitly later.
public sealed class SourceFoliageProbe : UObject
{
    public byte[] Tail = [];
    public long TailOffset;
    public override void Deserialize(FAssetArchive archive, long validPos)
    {
        base.Deserialize(archive, validPos);
        TailOffset = archive.Position;
        var length = checked((int)(validPos - archive.Position));
        if (length < 0 || length > 64 * 1024 * 1024)
            throw new InvalidDataException("Unexpected foliage payload size");
        Tail = archive.ReadBytes(length);
    }
}
