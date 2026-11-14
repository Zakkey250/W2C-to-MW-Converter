using System.Diagnostics;
using System.Numerics;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Common;
using Common.Geometry;
using Common.Geometry.Data;
using Common.Scenery.Data;
using Common.Textures.Data;
using Common.Lights.Data;

// Source-space IR import. Runtime conversion is a separate, validated stage.
// Original indices, attributes, all LOD references, flags/GUIDs and raw records
// remain available; no FBX, Assimp, quantization or native-game output is used.
static class SourceExport {
 static readonly JsonSerializerOptions Json=new(){IncludeFields=true};
 static string Sha(byte[] data)=>Convert.ToHexString(SHA256.HashData(data)).ToLowerInvariant();
 static void V(BinaryWriter w,Vector3 v){w.Write(v.X);w.Write(v.Y);w.Write(v.Z);}
 static void M(BinaryWriter w,Matrix4x4 m){foreach(float f in new[]{m.M11,m.M12,m.M13,m.M14,m.M21,m.M22,m.M23,m.M24,m.M31,m.M32,m.M33,m.M34,m.M41,m.M42,m.M43,m.M44})w.Write(f);}
 internal static byte[] Model(SolidObject s){
  using var ms=new MemoryStream();using var w=new BinaryWriter(ms,Encoding.UTF8,true);
  w.Write(Encoding.ASCII.GetBytes("W2SMOD1\0"));w.Write(1u);w.Write(128u);w.Write(s.Hash);w.Write(s.VertexSets.Count);w.Write(s.Materials.Count);w.Write(s.EnvironmentPositionMarkers.Count);w.Write(s is CarbonObject co?co.MorphTargets.Count:0);w.Write(0u);V(w,s.MinPoint);V(w,s.MaxPoint);M(w,s.PivotMatrix);
  if(ms.Position!=128)throw new InvalidDataException("Source model header");
  foreach(var set in s.VertexSets){w.Write(set.Count);w.Write(80u);foreach(var v in set){
   V(w,v.Position);w.Write(v.TexCoords.X);w.Write(v.TexCoords.Y);V(w,v.Normal??default);V(w,v.Tangent??default);V(w,v.BlendWeight??default);V(w,v.BlendIndices??default);w.Write(v.Color??0);w.Write(v.Color2??0);
   uint flags=(v.Normal.HasValue?1u:0)|(v.Tangent.HasValue?2u:0)|(v.BlendWeight.HasValue?4u:0)|(v.BlendIndices.HasValue?8u:0)|(v.Color.HasValue?16u:0)|(v.Color2.HasValue?32u:0);w.Write(flags);
  }}
  foreach(var a in s.Materials){
   if(a.VertexSetIndex<0||a.VertexSetIndex>=s.VertexSets.Count||a.Indices.Length%3!=0||a.Indices.Any(i=>i>=s.VertexSets[a.VertexSetIndex].Count))throw new InvalidDataException("Source index bounds");
   long start=ms.Position;var name=Encoding.UTF8.GetBytes(a.Name??"");w.Write(a.Hash);w.Write(a.Flags);w.Write(a.NumVerts);w.Write(a.VertexSetIndex);w.Write(a.Indices.Length);w.Write(a.DiffuseTextureHash);w.Write(a.NormalTextureHash??0);w.Write(a.SpecularTextureHash??0);w.Write((a.NormalTextureHash.HasValue?1u:0)|(a.SpecularTextureHash.HasValue?2u:0));w.Write(a is CarbonMaterial ca?ca.EffectId:0);w.Write(a is CarbonMaterial cb?cb.IndexStart:0);V(w,a.MinPoint);V(w,a.MaxPoint);w.Write(name.Length);w.Write(new byte[56]);
   if(ms.Position-start!=128)throw new InvalidDataException("Source material header");w.Write(name);while(ms.Position%4!=0)w.Write((byte)0);foreach(var i in a.Indices)w.Write(i);while(ms.Position%4!=0)w.Write((byte)0);
  }
  foreach(var marker in s.EnvironmentPositionMarkers){w.Write(marker.Length);w.Write(marker);while(ms.Position%4!=0)w.Write((byte)0);}
  if(s is CarbonObject morph)foreach(var h in morph.MorphTargets)w.Write(h);
  w.Flush();if(ms.Length>64*1024*1024)throw new InvalidDataException("Source model import bound");return ms.ToArray();
 }
 static byte[] Scene(ScenerySection s){
  using var ms=new MemoryStream();using var w=new BinaryWriter(ms,Encoding.UTF8,true);
  w.Write(Encoding.ASCII.GetBytes("W2SSCNE1"));w.Write(1u);w.Write(32u);w.Write(s.SectionNumber);w.Write(s.Infos.Count);w.Write(s.Instances.Count);w.Write(0u);
  foreach(var i in s.Infos){if(i.SourceRaw.Length!=72||i.LodKeys.Length!=4)throw new InvalidDataException("Complete source scenery info required");w.Write(i.SourceRaw);}
  foreach(var i in s.Instances){if(i.SourceRaw.Length!=96||i.InfoIndex<0||i.InfoIndex>=s.Infos.Count)throw new InvalidDataException("Complete source scenery instance required");w.Write(i.SourceRaw);}
  w.Flush();return ms.ToArray();
 }
 static object TextureMeta(Texture t)=>new{hash=t.TexHash.ToString("x8"),t.Name,t.TypeHash,t.Width,t.Height,t.MipMapCount,t.Format,compression=(byte)t.CompressionType,t.AlphaUsageType,t.AlphaBlendType,t.RenderFlags,t.TilableUV,t.ScrollType,t.ScrollTimeStep,t.ScrollSpeedS,t.ScrollSpeedT,t.OffsetS,t.OffsetT,t.ScaleS,t.ScaleT,sourceHeaderHex=Convert.ToHexString(t.SourceHeader).ToLowerInvariant(),sourceDataSha256=Sha(t.Data),sourcePaletteSha256=t.Palette is null?null:Sha(t.Palette)};
 sealed class Container:IDisposable {
  readonly FileStream stream;readonly StreamWriter index;public long Count{get;private set;}
  public Container(string root,string name){stream=new FileStream(Path.Combine(root,name+".sourceir"),FileMode.CreateNew,FileAccess.Write,FileShare.Read);index=new StreamWriter(new FileStream(Path.Combine(root,name+".jsonl"),FileMode.CreateNew),new UTF8Encoding(false));}
  public void Add(string key,byte[] data,object metadata){while(stream.Position%64!=0)stream.WriteByte(0);long offset=stream.Position;stream.Write(data);index.WriteLine(JsonSerializer.Serialize(new{key,offset,bytes=data.LongLength,sha256=Sha(data),metadata},Json));Count++;}
  public void Dispose(){index.Dispose();stream.Dispose();}
 }
 public static int Run(string configPath,string output){
  var started=Stopwatch.StartNew();var config=JsonDocument.Parse(File.ReadAllBytes(configPath));output=Path.GetFullPath(output);if(Directory.Exists(output)&&Directory.EnumerateFileSystemEntries(output).Any())throw new IOException("Immutable source-IR trial exists");Directory.CreateDirectory(output);
  using(var models=new Container(output,"models"))using(var scenes=new Container(output,"scenes"))using(var textures=new Container(output,"textures"))using(var lights=new Container(output,"lights"))using(var systems=new Container(output,"systems")){
   var modelHashes=new Dictionary<uint,string>();var textureHashes=new Dictionary<uint,string>();var sourceRows=new List<object>();var invalid=new List<object>();var textureRefs=new HashSet<uint>();var modelRefs=new HashSet<uint>();var textureVariants=new List<object>();
   foreach(var input in config.RootElement.GetProperty("files").EnumerateArray()){
    var role=input.GetProperty("role").GetString()!;bool selected=input.TryGetProperty("import",out var explicitImport)?explicitImport.GetBoolean():role is "x360" or "global-globalb" or "global-ingamea" or "global-ingameb";if(!selected)continue;var path=input.GetProperty("path").GetString()!;string fileHash;using(var f=File.OpenRead(path))fileHash=Convert.ToHexString(SHA256.HashData(f)).ToLowerInvariant();sourceRows.Add(new{role,path,bytes=new FileInfo(path).Length,sha256=fileHash});Console.WriteLine(JsonSerializer.Serialize(new{phase="source-ir-import",role,path}));Console.Out.Flush();
    var cm=new ChunkManager(GameDetector.Game.Carbon);cm.Read(path);
    if (input.TryGetProperty("missingTextureFallbacks", out var fallbacks))
    {
     var remaining = fallbacks.EnumerateObject()
      .ToDictionary(item => Convert.ToUInt32(item.Name, 16), item => item.Value.GetString());
     foreach (var existing in textureHashes.Keys)
      remaining.Remove(existing);
     foreach (var chunk in cm.Chunks)
     {
      if (chunk.Resource is not TexturePack pack)
       continue;
      foreach (var texture in pack.Textures)
      {
       if (!remaining.TryGetValue(texture.TexHash, out var expectedHash))
        continue;
       using var image = new MemoryStream();
       texture.GenerateImage(image);
       var data = image.ToArray();
       var hash = Sha(data);
       if (hash != expectedHash)
        continue;
       textureHashes.Add(texture.TexHash, hash);
       var origin = new { role, offset = chunk.Offset, chunk = chunk.Id.ToString("x8") };
       textures.Add(texture.TexHash.ToString("x8"), data, new { origin, texture = TextureMeta(texture) });
       remaining.Remove(texture.TexHash);
      }
     }
     if (remaining.Count != 0)
      throw new InvalidDataException("Carbon texture data did not match the supported bank: " + role);
     continue;
    }
    foreach(var c in cm.Chunks){
     var origin=new{role,offset=c.Offset,chunk=c.Id.ToString("x8")};
     if(c.Resource is SolidList sl)foreach(var s in sl.Objects){var data=Model(s);var hash=Sha(data);if(modelHashes.TryGetValue(s.Hash,out var old)){if(hash!=old)throw new InvalidDataException("Ambiguous source model hash "+s.Hash.ToString("x8"));continue;}modelHashes[s.Hash]=hash;models.Add(s.Hash.ToString("x8"),data,new{origin,s.Name,sets=s.VertexSets.Count,materials=s.Materials.Count});int bad=s.VertexSets.Sum(vs=>vs.Count(v=>!float.IsFinite(v.Position.X)||!float.IsFinite(v.Position.Y)||!float.IsFinite(v.Position.Z)));if(bad>0)invalid.Add(new{model=s.Hash.ToString("x8"),s.Name,invalidPositionVertices=bad,sourceBitsPreserved=true});foreach(var a in s.Materials){textureRefs.Add(a.DiffuseTextureHash);if(a.NormalTextureHash.HasValue)textureRefs.Add(a.NormalTextureHash.Value);if(a.SpecularTextureHash.HasValue)textureRefs.Add(a.SpecularTextureHash.Value);}}
     else if(c.Resource is ScenerySection s){scenes.Add(s.SectionNumber.ToString(),Scene(s),new{origin,s.SectionNumber,infos=s.Infos.Count,placements=s.Instances.Count});foreach(var i in s.Infos)foreach(var h in i.LodKeys)if(h!=0)modelRefs.Add(h);}
     else if(c.Resource is TexturePack tp)foreach(var t in tp.Textures){using var ms=new MemoryStream();t.GenerateImage(ms);var data=ms.ToArray();var hash=Sha(data);string key=t.TexHash.ToString("x8");if(textureHashes.TryGetValue(t.TexHash,out var old)){if(old==hash)continue;key+="/variant/"+role+"/"+c.Offset;textureVariants.Add(new{hash=t.TexHash.ToString("x8"),canonicalSha256=old,alternateSha256=hash,origin,rule="Preserve alternate TPK namespace; canonical lookup uses primary world stream then first base-global definition"});}else textureHashes[t.TexHash]=hash;textures.Add(key,data,new{origin,texture=TextureMeta(t)});}
     else if(c.Resource is LightPack lp){lights.Add(role+"/"+lp.ScenerySectionNumber,JsonSerializer.SerializeToUtf8Bytes(lp,Json),new{origin,section=lp.ScenerySectionNumber,count=lp.Lights.Count});}
     else if(c.Id!=0){
      // Exact source bytes retained for the independent typed system decoders.
      using var f=File.OpenRead(path);f.Position=c.Offset+8;var data=new byte[c.Size];f.ReadExactly(data);systems.Add(role+"/"+c.Offset,data,new{origin,encoding="original-chunk-payload",classification="pending-typed-system-decoder"});
     }
    }
    cm=null!;GC.Collect();GC.WaitForPendingFinalizers();
   }
   var missingModels=modelRefs.Where(h=>!modelHashes.ContainsKey(h)).Select(h=>h.ToString("x8")).OrderBy(h=>h).ToArray();var missingTextures=textureRefs.Where(h=>h!=0&&!textureHashes.ContainsKey(h)).Select(h=>h.ToString("x8")).OrderBy(h=>h).ToArray();
   var sourceVariant=config.RootElement.TryGetProperty("sourceVariant",out var variant)?variant.GetString():"x360";
   File.WriteAllBytes(Path.Combine(output,"import-summary.json"),JsonSerializer.SerializeToUtf8Bytes(new{schema=1,stage="source-space-import-only",sourceVariant,sourceRows,models=models.Count,scenes=scenes.Count,textures=textures.Count,lightPacks=lights.Count,systemChunks=systems.Count,textureVariants,invalidSourceModels=invalid,missingModels,missingTextures,geometryQuantized=false,sourceSceneryAllLodsFlagsGuidsPreserved=true,classificationComplete=false,conversionComplete=false,elapsedSeconds=started.Elapsed.TotalSeconds},new JsonSerializerOptions(Json){WriteIndented=true}));
   Console.WriteLine(JsonSerializer.Serialize(new{phase="source-ir-import-complete",models=models.Count,scenes=scenes.Count,textures=textures.Count,missingModels=missingModels.Length,missingTextures=missingTextures.Length,output}));
  }
  return 0;
 }
}
