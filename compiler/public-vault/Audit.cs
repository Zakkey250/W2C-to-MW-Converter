using System.Collections;
using System.Runtime.CompilerServices;
using System.Text.Json;
using VaultLib.Core;
using VaultLib.Core.DB;
using VaultLib.Core.Hashing;
using VaultLib.Core.Pack;
using VaultLib.Core.Types;

internal static class Audit
{
    [MethodImpl(MethodImplOptions.NoInlining)]
    internal static int Run(string game, string directory, string output, string? recipe, string dictionary)
    {
        HashManager.LoadDictionary(dictionary);
        if (recipe is not null)
        {
            using var plan = JsonDocument.Parse(File.ReadAllBytes(recipe));
            foreach (var row in plan.RootElement.EnumerateArray())
                HashManager.AddVLT(row.GetProperty("target").GetString()!);
        }

        switch (game)
        {
            case "MOST_WANTED": new VaultLib.Support.MostWanted.ModuleDef().Load(); break;
            case "CARBON": new VaultLib.Support.Carbon.ModuleDef().Load(); break;
            default: throw new ArgumentException("Unsupported vault format.", nameof(game));
        }

        var db = new Database(new DatabaseOptions(game, DatabaseType.X86Database));
        foreach (var name in new[] { "attributes.bin", "fe_attrib.bin", "gameplay.bin" })
        {
            using var reader = new BinaryReader(File.OpenRead(Path.Combine(directory, name)));
            new StandardVaultPack().Load(reader, db, new PackLoadingOptions());
        }
        db.CompleteLoad();

        var rows = db.RowManager.EnumerateFlattenedCollections()
            .Select(row => new
            {
                @class = row.Class.Name,
                name = row.Name,
                hash = (row.Name.StartsWith("0x", StringComparison.OrdinalIgnoreCase) && row.Name.Length == 10
                    ? Convert.ToUInt32(row.Name[2..], 16)
                    : VLT32Hasher.Hash(row.Name, 0xABCDEF00, false)).ToString("X8"),
                parent = row.Parent?.Name,
                fields = row.GetData().OrderBy(field => field.Key).ToDictionary(field => field.Key, field => Format(field.Value))
            })
            .OrderBy(row => row.@class).ThenBy(row => row.name);
        using var file = File.Open(output, FileMode.CreateNew);
        JsonSerializer.Serialize(file, new { rows });
        return 0;
    }

    private static string Format(object? value) => value switch
    {
        null => "null",
        string text => text,
        VLTArrayType array => "[" + string.Join(",", array.Items.Select(Format)) + "]",
        IEnumerable items => "[" + string.Join(",", items.Cast<object>().Select(Format)) + "]",
        _ => value.ToString() ?? ""
    };
}
