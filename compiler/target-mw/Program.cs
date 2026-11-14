// Read-only MW base resources used by the new native-physics adapter. World
// terrain and W2C assets still come exclusively from the selected X360 input.
using System.Numerics;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Common;
using Common.Geometry.Data;
using Common.Scenery.Data;
using Common.Textures.Data;

public static class TargetImport {
 static readonly JsonSerializerOptions Json=new(){IncludeFields=true};
 static string Sha(byte[] b)=>Convert.ToHexString(SHA256.HashData(b)).ToLowerInvariant();
 static void Matrix(BinaryWriter w,Matrix4x4 m){foreach(var x in new[]{m.M11,m.M12,m.M13,m.M14,m.M21,m.M22,m.M23,m.M24,m.M31,m.M32,m.M33,m.M34,m.M41,m.M42,m.M43,m.M44})w.Write(x);}
 static void V(BinaryWriter w,Vector3 v){w.Write(v.X);w.Write(v.Y);w.Write(v.Z);}
 static byte[] Scene(ScenerySection s){using var ms=new MemoryStream();using var w=new BinaryWriter(ms);w.Write(Encoding.ASCII.GetBytes("W2TSCN1\0"));w.Write(1);w.Write(32);w.Write(s.SectionNumber);w.Write(s.Infos.Count);w.Write(s.Instances.Count);w.Write(0);
  foreach(var i in s.Infos){var name=Encoding.UTF8.GetBytes(i.Name??"");w.Write(i.SolidKey);w.Write(name.Length);w.Write(name);while(ms.Position%4!=0)w.Write((byte)0);}
  foreach(var i in s.Instances){w.Write(i.Flags);w.Write(i.InfoIndex);Matrix(w,i.Transform);V(w,i.SourceBoundsMin);V(w,i.SourceBoundsMax);}return ms.ToArray();}
 sealed class Output:IDisposable {
  readonly FileStream f;readonly StreamWriter index;
  public Output(string root,string name){f=new FileStream(Path.Combine(root,name+".sourceir"),FileMode.CreateNew);index=new StreamWriter(new FileStream(Path.Combine(root,name+".jsonl"),FileMode.CreateNew),new UTF8Encoding(false));}
  public void Add(string key,byte[] data,object metadata){while(f.Position%64!=0)f.WriteByte(0);long offset=f.Position;f.Write(data);index.WriteLine(JsonSerializer.Serialize(new{key,offset,bytes=data.Length,sha256=Sha(data),metadata},Json));}
  public void Dispose(){index.Dispose();f.Dispose();}
 }
 public static int Run(string[] args){try{
  if(args.Length!=2)throw new ArgumentException("TargetMW CONFIG OUTPUT");var config=JsonDocument.Parse(File.ReadAllBytes(args[0]));var output=Path.GetFullPath(args[1]);if(Directory.Exists(output)&&Directory.EnumerateFileSystemEntries(output).Any())throw new IOException("Immutable target input trial exists");Directory.CreateDirectory(output);
  // The pinned, independently verified model attribute serializer is reused;
  // no old World2MW converter or deployed asset is called or included.
  var seenModels=new Dictionary<uint,string>();var seenTextures=new Dictionary<uint,string>();var sources=new List<object>();long scenes=0,textures=0,systems=0;
  HashSet<uint>? wantedTextures=config.RootElement.TryGetProperty("textureHashes",out var filter)?filter.EnumerateArray().Select(v=>Convert.ToUInt32(v.GetString(),16)).ToHashSet():null;
  HashSet<uint>? wantedModels=config.RootElement.TryGetProperty("modelHashes",out var modelFilter)?modelFilter.EnumerateArray().Select(v=>Convert.ToUInt32(v.GetString(),16)).ToHashSet():null;
  bool includeSystems=!config.RootElement.TryGetProperty("includeSystems",out var systemFilter)||systemFilter.GetBoolean();
  bool carbonSprites=config.RootElement.TryGetProperty("carbonSprites",out var spriteMode)&&spriteMode.GetBoolean();
  if(carbonSprites&&(wantedTextures is null||wantedModels?.Count!=0||includeSystems))throw new ArgumentException("Carbon input is limited to explicitly selected textures");
  using(var models=new Output(output,"models"))using(var sceneOut=new Output(output,"scenes"))using(var textureOut=new Output(output,"textures"))using(var systemOut=new Output(output,"systems")){
   foreach(var row in config.RootElement.GetProperty("files").EnumerateArray()){
    var role=row.GetProperty("role").GetString()!;var path=row.GetProperty("path").GetString()!;string sourceSha;using(var f=File.OpenRead(path))sourceSha=Convert.ToHexString(SHA256.HashData(f)).ToLowerInvariant();sources.Add(new{role,path,bytes=new FileInfo(path).Length,sha256=sourceSha});Console.WriteLine(JsonSerializer.Serialize(new{phase="read-target-base",role,path}));Console.Out.Flush();
    var cm=new ChunkManager(carbonSprites?GameDetector.Game.Carbon:GameDetector.Game.MostWanted);cm.Read(path);
    foreach(var c in cm.Chunks){var origin=new{role,offset=c.Offset,chunk=c.Id.ToString("x8")};
     if(c.Resource is SolidList sl)foreach(var s in sl.Objects){if(wantedModels is not null&&!wantedModels.Contains(s.Hash))continue;var data=SourceExport.Model(s);var digest=Sha(data);var key=s.Hash.ToString("x8");if(seenModels.TryGetValue(s.Hash,out var old)){if(old==digest)continue;key+="/variant/"+role+"/"+c.Offset;}else seenModels[s.Hash]=digest;models.Add(key,data,new{origin,s.Name,namespacePolicy="World stream first; distinct global/UI variants retained"});}
     else if(c.Resource is ScenerySection s){sceneOut.Add(s.SectionNumber.ToString(),Scene(s),new{origin,s.SectionNumber,infos=s.Infos.Count,placements=s.Instances.Count});scenes++;}
     else if(c.Resource is TexturePack tp)foreach(var t in tp.Textures){if(wantedTextures is not null&&!wantedTextures.Contains(t.TexHash))continue;using var ms=new MemoryStream();t.GenerateImage(ms);var data=ms.ToArray();var digest=Sha(data);var key=t.TexHash.ToString("x8");if(seenTextures.TryGetValue(t.TexHash,out var old)){if(old==digest)continue;key+="/variant/"+role+"/"+c.Offset;}else seenTextures[t.TexHash]=digest;textureOut.Add(key,data,new{origin,texture=new{hash=t.TexHash.ToString("x8"),t.Name,t.TypeHash,t.Width,t.Height,t.MipMapCount,t.Format,t.AlphaUsageType,t.AlphaBlendType,t.RenderFlags,t.TilableUV,t.ScrollType,t.ScrollTimeStep,t.ScrollSpeedS,t.ScrollSpeedT,t.OffsetS,t.OffsetT,t.ScaleS,t.ScaleT,sourceHeaderHex=Convert.ToHexString(t.SourceHeader).ToLowerInvariant(),sourceDataSha256=Sha(t.Data)}});textures++;}
     else if(includeSystems&&c.Id!=0){using var f=File.OpenRead(path);f.Position=c.Offset+8;var data=new byte[c.Size];f.ReadExactly(data);systemOut.Add(role+"/"+c.Offset,data,new{origin});systems++;}
    }
    cm=null!;GC.Collect();GC.WaitForPendingFinalizers();
   }
  }
  File.WriteAllBytes(Path.Combine(output,"import-summary.json"),JsonSerializer.SerializeToUtf8Bytes(new{schema=1,scope="MW base-resource adapter only; W2C world quality remains X360",sources,models=seenModels.Count,scenes,textures,systems},new JsonSerializerOptions(Json){WriteIndented=true}));Console.WriteLine(JsonSerializer.Serialize(new{phase="target-base-import-complete",models=seenModels.Count,scenes,textures,systems}));return 0;
 }catch(Exception e){Console.Error.WriteLine(JsonSerializer.Serialize(new{error=e.ToString()}));return 1;}}
}
