using LibProsperoPkg;
using LibProsperoPkg.PKG;

if (args.Length != 2) return 2;
    const string contentId = "IV0000-ACTV00002_00-PLAYLOGTRACKER01";
Directory.CreateDirectory(args[1]);
var result = ProsperoPackageBuilder.Build(new ProsperoBuildOptions
{
    Mode = ProsperoPackageMode.Application,
    OutputFormat = ProsperoOutputFormat.DebugImage,
    SourceFolder = Path.GetFullPath(args[0]),
    OutputFolder = Path.GetFullPath(args[1]),
    ContentId = contentId,
    TitleId = "ACTV00002",
    Title = "Playlog",
    Version = "01.49",
    GenerateParamJsonIfMissing = false,
    LicenseFree = true
}, Console.WriteLine);
Console.WriteLine($"Package: {result.OutputPath}");
var report = ProsperoPkgValidator.Validate(ProsperoPkgReader.Read(result.OutputPath));
Console.WriteLine($"Accepted: {report.Accepted}");
foreach (var check in report.Checks)
    Console.WriteLine($"{check.Status}: {check.Name} - {check.Detail}");
return report.Accepted ? 0 : 1;
