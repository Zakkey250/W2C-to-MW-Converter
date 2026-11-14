using System.Diagnostics;
using System.Numerics;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Common;
using Common.Geometry.Data;
using Common.Scenery.Data;
using Common.Textures.Data;
using Common.Lights.Data;

static class Program {
 static readonly JsonSerializerOptions Json=new(){WriteIndented=false,IncludeFields=true};
 static string Hash(byte[] bytes)=>Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
 static string FileHash(string p){using var f=File.OpenRead(p);return Convert.ToHexString(SHA256.HashData(f)).ToLowerInvariant();}
 static void Vec(BinaryWriter w,Vector3 v){w.Write(v.X);w.Write(v.Y);w.Write(v.Z);}
 static void NullableVec(BinaryWriter w,Vector3? v){w.Write(v.HasValue);if(v.HasValue)Vec(w,v.Value);}
 static void Matrix(BinaryWriter w,Matrix4x4 m){foreach(var v in new[]{m.M11,m.M12,m.M13,m.M14,m.M21,m.M22,m.M23,m.M24,m.M31,m.M32,m.M33,m.M34,m.M41,m.M42,m.M43,m.M44})w.Write(v);}
 static (string digest,int invalid,long vertices,long triangles) ModelDigest(SolidObject s){
  using var m=new MemoryStream();using var w=new BinaryWriter(m,Encoding.UTF8,true);
  w.Write(s.Hash);w.Write(s.Name??"");Matrix(w,s.PivotMatrix);Vec(w,s.MinPoint);Vec(w,s.MaxPoint);
  w.Write(s.VertexSets.Count);long vc=0,tris=0;int invalid=0;
  foreach(var set in s.VertexSets){w.Write(set.Count);vc+=set.Count;foreach(var v in set){Vec(w,v.Position);w.Write(v.TexCoords.X);w.Write(v.TexCoords.Y);NullableVec(w,v.Normal);NullableVec(w,v.Tangent);NullableVec(w,v.BlendWeight);NullableVec(w,v.BlendIndices);w.Write(v.Color.HasValue);if(v.Color.HasValue)w.Write(v.Color.Value);w.Write(v.Color2.HasValue);if(v.Color2.HasValue)w.Write(v.Color2.Value);if(!float.IsFinite(v.Position.X)||!float.IsFinite(v.Position.Y)||!float.IsFinite(v.Position.Z))invalid++;}}
  w.Write(s.Materials.Count);foreach(var a in s.Materials){if(a.VertexSetIndex<0||a.VertexSetIndex>=s.VertexSets.Count||a.Indices.Length%3!=0||a.Indices.Any(i=>i>=s.VertexSets[a.VertexSetIndex].Count))throw new InvalidDataException("Invalid indexed source model "+s.Name);w.Write(a.Hash);w.Write(a.Flags);w.Write(a.Name??"");w.Write(a.VertexSetIndex);w.Write(a.DiffuseTextureHash);w.Write(a.NormalTextureHash.HasValue);if(a.NormalTextureHash.HasValue)w.Write(a.NormalTextureHash.Value);w.Write(a.SpecularTextureHash.HasValue);if(a.SpecularTextureHash.HasValue)w.Write(a.SpecularTextureHash.Value);w.Write(a.Indices.Length);foreach(var ix in a.Indices)w.Write(ix);tris+=a.Indices.Length/3;}
  w.Flush();return(Hash(m.ToArray()),invalid,vc,tris);
 }
 static object AuditFile(string path,string role){
  if(!File.Exists(path))throw new FileNotFoundException(path);
  var info=new FileInfo(path);if(info.Length>2_147_483_647)throw new InvalidDataException("Current source reader file bound");
  Console.WriteLine(JsonSerializer.Serialize(new{phase="reading-original",role,path,bytes=info.Length}));Console.Out.Flush();
  var sourceSha=FileHash(path);var timer=Stopwatch.StartNew();var cm=new ChunkManager(GameDetector.Game.Carbon);cm.Read(path);
  var models=new List<object>();var textures=new List<object>();var scenes=new List<object>();var lights=new List<object>();var chunks=new List<object>();
  long vertices=0,triangles=0,placements=0;int invalid=0;
  foreach(var c in cm.Chunks){
   string kind=c.Resource?.GetType().FullName??"uninterpreted";
   chunks.Add(new{id=c.Id.ToString("x8"),offset=c.Offset,bytes=c.Size,resource=kind,childCount=c.SubChunks?.Count??0,rawDataBytes=c.Data?.Length??0});
   if(c.Resource is SolidList list)foreach(var s in list.Objects){var d=ModelDigest(s);vertices+=d.vertices;triangles+=d.triangles;invalid+=d.invalid;models.Add(new{sourceChunkOffset=c.Offset,hash=s.Hash.ToString("x8"),s.Name,vertexSets=s.VertexSets.Count,vertices=d.vertices,triangles=d.triangles,materials=s.Materials.Count,semanticSha256=d.digest,invalidPositionVertices=d.invalid,morphTargets=s is CarbonObject co?co.MorphTargets.Count:0,environmentMarkers=s.EnvironmentPositionMarkers.Count,textureHashes=s.TextureHashes.Select(x=>x.ToString("x8")).ToArray()});}
   if(c.Resource is TexturePack tpk)foreach(var t in tpk.Textures){textures.Add(new{sourceChunkOffset=c.Offset,hash=t.TexHash.ToString("x8"),t.Name,t.Width,t.Height,t.MipMapCount,t.Format,t.CompressionType,t.AlphaUsageType,t.AlphaBlendType,t.RenderFlags,t.TilableUV,t.ScrollType,t.ScrollTimeStep,t.ScrollSpeedS,t.ScrollSpeedT,bytes=t.Data.LongLength,dataSha256=Hash(t.Data),paletteSha256=t.Palette is null?null:Hash(t.Palette)});}
   if(c.Resource is ScenerySection s2){using var ms=new MemoryStream();using var w=new BinaryWriter(ms);w.Write(s2.SectionNumber);foreach(var i in s2.Infos){w.Write(i.SolidKey);w.Write(i.Name??"");w.Write(i.IsDeinstanced);}foreach(var i in s2.Instances){w.Write(i.InfoIndex);w.Write(i.Flags);Matrix(w,i.Transform);Vec(w,i.SourceBoundsMin);Vec(w,i.SourceBoundsMax);}w.Flush();placements+=s2.Instances.Count;scenes.Add(new{sourceChunkOffset=c.Offset,section=s2.SectionNumber,infos=s2.Infos.Count,placements=s2.Instances.Count,semanticSha256=Hash(ms.ToArray()),flags=s2.Instances.GroupBy(x=>x.Flags).ToDictionary(x=>x.Key.ToString("x8"),x=>x.Count()),solidReferences=s2.Infos.Select(i=>i.SolidKey.ToString("x8")).Distinct().ToArray()});}
   if(c.Resource is LightPack lp)lights.Add(new{sourceChunkOffset=c.Offset,section=lp.ScenerySectionNumber,count=lp.Lights.Count,semanticSha256=Hash(JsonSerializer.SerializeToUtf8Bytes(lp,Json))});
  }
  var process=Process.GetCurrentProcess();process.Refresh();
  return new{path,role,bytes=info.Length,sourceSha256=sourceSha,chunks,models,textures,scenes,lights,totalVertices=vertices,totalTriangles=triangles,totalPlacements=placements,invalidPositionVertices=invalid,elapsedSeconds=timer.Elapsed.TotalSeconds,peakWorkingSetBytes=process.PeakWorkingSet64,privateBytes=process.PrivateMemorySize64,rawOriginalWritten=false,classificationComplete=false};
 }
 public static int Main(string[] args){try{
  if(args.Length==3&&args[0]=="export")return SourceExport.Run(args[1],args[2]);
  if(args.Length==3&&args[0]=="mw-target")return TargetImport.Run(args[1..]);
  if(args.Length!=2)throw new ArgumentException("SourceWorld CONFIG OUTPUT_DIRECTORY");
  var config=JsonDocument.Parse(File.ReadAllBytes(args[0]));var output=Path.GetFullPath(args[1]);Directory.CreateDirectory(output);
  foreach(var row in config.RootElement.GetProperty("files").EnumerateArray()){
   var role=row.GetProperty("role").GetString()!;if(role.Any(c=>!char.IsAsciiLetterOrDigit(c)&&c!='-'))throw new InvalidDataException("Output role");var dest=Path.Combine(output,role+"-audit.json");if(File.Exists(dest))throw new IOException("Immutable audit already exists "+dest);
   var audit=AuditFile(row.GetProperty("path").GetString()!,role);File.WriteAllBytes(dest,JsonSerializer.SerializeToUtf8Bytes(audit,Json));Console.WriteLine(JsonSerializer.Serialize(new{phase="audit-complete",role,destination=dest}));Console.Out.Flush();GC.Collect();GC.WaitForPendingFinalizers();
  }
  return 0;
 }catch(Exception e){Console.Error.WriteLine(JsonSerializer.Serialize(new{error=e.ToString()}));return 1;}}
}
