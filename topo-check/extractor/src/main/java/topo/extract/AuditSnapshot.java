package topo.extract;

import com.google.gson.*;
import java.io.*;
import java.nio.file.*;
import java.security.*;
import java.util.*;

/** Bounded before/after content checks. This does not claim an OS sandbox. */
final class AuditSnapshot {
    private final Map<String,String> digests=new TreeMap<>();
    private final List<String> errors=new ArrayList<>();
    private long bytes;
    AuditSnapshot(Map<String,String> sources,String[] classpath,String[] sourcepath) {
        for(var e:sources.entrySet())digests.put(AuditOrigins.canonical(e.getKey()),hash(e.getValue().getBytes(java.nio.charset.StandardCharsets.UTF_8)));
        for(String p:classpath)addTree(Path.of(p),false);
        // JDT resolves Java declarations from source roots. Reports, build
        // outputs and unrelated resources in those roots are not parser inputs.
        for(String p:sourcepath)addTree(Path.of(p),true);
        for(Class<?> type:List.of(Main.class,org.eclipse.jdt.core.dom.ASTParser.class,Gson.class))try {
            addTree(Path.of(type.getProtectionDomain().getCodeSource().getLocation().toURI()),false);
        } catch(Exception e) {errors.add("Cannot snapshot analyzer: "+e.getMessage());}
        Path jdk=Path.of(System.getProperty("java.home"));
        for(String path:List.of("release","lib/modules","bin/java","bin/java.exe"))
            if(Files.isRegularFile(jdk.resolve(path)))addTree(jdk.resolve(path),false);
    }
    private static String hash(byte[] data) {
        try{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(data));}
        catch(NoSuchAlgorithmException e){throw new IllegalStateException(e);}
    }
    private String hashFile(Path file)throws IOException {
        try {
            MessageDigest digest=MessageDigest.getInstance("SHA-256");
            try(InputStream in=Files.newInputStream(file)) {
                byte[] buffer=new byte[65536];int count;
                while((count=in.read(buffer))!=-1) {
                    bytes+=count;
                    if(bytes>16L*1024*1024*1024)throw new IOException("Java analysis input hashing exceeds 16 GiB limit");
                    digest.update(buffer,0,count);
                }
            }
            return HexFormat.of().formatHex(digest.digest());
        }catch(NoSuchAlgorithmException e){throw new IllegalStateException(e);}
    }
    private void addTree(Path path,boolean sourcesOnly) {
        try {
            if(Files.isDirectory(path))try(var files=Files.walk(path)) {
                var iterator=files.filter(Files::isRegularFile)
                    .filter(p->!sourcesOnly || p.toString().endsWith(".java")).iterator();
                while(iterator.hasNext())addFile(iterator.next());
            }else addFile(path);
        }catch(IOException e){errors.add("Cannot snapshot input "+path+": "+e.getMessage());}
    }
    private void addFile(Path path)throws IOException {
        String key=AuditOrigins.canonical(path.toString());
        if(digests.containsKey(key))return;
        if(digests.size()>=100000)throw new IOException("Java analysis exceeds 100000 input files");
        digests.put(key,hashFile(path));
    }
    void finish(JsonObject audit) {
        bytes=0;
        for(var e:digests.entrySet())try {
            if(!e.getValue().equals(hashFile(Path.of(e.getKey()))))errors.add("Input changed during Java analysis: "+e.getKey());
        }catch(IOException ex){errors.add("Cannot recheck Java analysis input: "+e.getKey()+": "+ex.getMessage());}
        for(JsonElement input:audit.getAsJsonArray("inputs"))if(!digests.containsKey(AuditOrigins.canonical(input.getAsString())))
            errors.add("Java analysis consumed an input outside its before/after snapshot: "+input.getAsString());
        JsonObject values=new JsonObject();digests.forEach(values::addProperty);audit.add("inputDigests",values);
        audit.addProperty("analysisIsolation","bounded-process");
        JsonArray allInputs=new JsonArray();digests.keySet().forEach(allInputs::add);audit.add("inputs",allInputs);
        for(String error:errors)audit.getAsJsonArray("unresolved").add(error);
        if(!errors.isEmpty())audit.addProperty("complete",false);
    }
}
