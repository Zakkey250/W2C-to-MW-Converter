using System.Globalization;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text.Json;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
if (args.Length < 3)
    throw new ArgumentException("PublicVault <selected tool directory> <audit|merge> <arguments>");

var tool = Path.GetFullPath(args[0]);
var profiles = Path.Combine(tool, "plugins", "Attribulator.Plugins.SpeedProfiles");
var directories = new[] { tool, profiles };
AssemblyLoadContext.Default.Resolving += (context, name) =>
{
    foreach (var directory in directories)
    {
        var dependency = Path.Combine(directory, name.Name + ".dll");
        if (File.Exists(dependency))
            return context.LoadFromAssemblyPath(dependency);
    }
    return null;
};
AppContext.SetData("APP_CONTEXT_BASE_DIRECTORY", tool + Path.DirectorySeparatorChar);

try
{
    if (args[1] == "audit" && args.Length is 5 or 6)
    {
        var assembly = AssemblyLoadContext.Default.LoadFromAssemblyName(new AssemblyName("VaultLib.Core"));
        NativeLibrary.SetDllImportResolver(assembly, (name, _, _) =>
            name is "NativeLibrary" or "NativeLibrary.dll"
                ? NativeLibrary.Load(Path.Combine(tool, "NativeLibrary.dll"))
                : IntPtr.Zero);
        return Audit.Run(args[2], args[3], args[4], args.ElementAtOrDefault(5),
                         Path.Combine(profiles, "Resources", "hashes.txt"));
    }
    if (args[1] == "merge" && args.Length is 5 or 6)
    {
        var game = args.ElementAtOrDefault(5) ?? "MOST_WANTED";
        if (game is not ("MOST_WANTED" or "CARBON"))
            throw new ArgumentException("Unsupported vault format.");
        var assembly = AssemblyLoadContext.Default.LoadFromAssemblyPath(Path.Combine(tool, "Attribulator.CLI.dll"));
        var entry = assembly.EntryPoint ?? throw new InvalidOperationException("Attribulator entry point missing.");
        string[] command = ["apply-script-bin", "-i", args[2], "-o", args[3], "-p", game, "-s", args[4]];
        var result = entry.Invoke(null, [command]);
        if (result is Task task)
        {
            await task;
            result = task.GetType().GetProperty("Result")?.GetValue(task);
        }
        return result is int exitCode ? exitCode : 0;
    }
    throw new ArgumentException("Unsupported vault operation.");
}
catch (Exception error)
{
    Console.Error.WriteLine(error is TargetInvocationException { InnerException: { } inner } ? inner : error);
    return 1;
}
