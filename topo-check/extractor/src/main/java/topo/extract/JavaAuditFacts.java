package topo.extract;

import com.google.gson.*;
import org.eclipse.jdt.core.dom.*;
import java.util.*;

/** Versioned, binding-backed audit facts. No target code is loaded or executed. */
final class JavaAuditFacts {
    private final AuditOrigins origins;
    private final LinkedHashMap<String,JsonObject> identities=new LinkedHashMap<>();
    private final JsonArray references=new JsonArray(), nativeBindings=new JsonArray();
    private final LinkedHashSet<String> unresolved=new LinkedHashSet<>();
    private final NativeBindings nativeResolver;
    JavaAuditFacts(Map<String,CompilationUnit> units,String[] classpath,String[] sourcepath,JsonObject request) {
        origins=new AuditOrigins(units,classpath,sourcepath,request);
        nativeResolver=new NativeBindings(units,this);
    }
    void unknown(ASTNode n,String reason) {
        unresolved.add(SourceContext.file()+":"+SourceContext.line(n)+": "+reason);
    }
    private JsonObject identity(String id,String name,String kind,String signature,String owner,
                                String file,int line,String origin,boolean complete) {
        JsonObject out=new JsonObject();out.addProperty("id",id);out.addProperty("name",name);out.addProperty("kind",kind);
        out.addProperty("signature",signature);out.addProperty("owner",owner);out.addProperty("file",file);
        out.addProperty("line",line);out.addProperty("origin",origin);out.addProperty("complete",complete);
        // Source declarations replace a referenced declaration's location when
        // its compilation unit is encountered later in the batch.
        if(!identities.containsKey(id) || line>0) identities.put(id,out);
        return out;
    }
    String typeIdentity(ITypeBinding type,ASTNode declaration) {
        if(type==null || type.isRecovered()) return "";
        String origin=origins.origin(type),id=origins.typeId(type);
        if(id.isEmpty()) return "";
        String owner=type.getDeclaringClass()==null?"":typeIdentity(type.getDeclaringClass(),null);
        String name=type.getQualifiedName();if(name.isEmpty())name=type.getBinaryName();
        boolean source=type.isFromSource();
        if(!source)return id;
        boolean complete=!origin.isEmpty() && (!source || origins.analyzed(origin));
        identity(id,name,type.isEnum()?"enum":type.isRecord()?"record":type.isInterface()?"interface":"class","",owner,source?origin:"",
            declaration==null?0:SourceContext.line(declaration),origin,complete);
        return id;
    }
    String methodIdentity(IMethodBinding method,ASTNode declaration) {
        if(method==null || method.isRecovered() || method.getDeclaringClass()==null) return "";
        method=method.getMethodDeclaration();
        String owner=typeIdentity(method.getDeclaringClass(),null),origin=origins.origin(method.getDeclaringClass());
        String id=origins.methodId(method);if(id.isEmpty())return "";
        boolean source=method.getDeclaringClass().isFromSource();
        if(!source)return id;
        identity(id,SourceContext.qualified(method),method.isConstructor()?"constructor":"method",SourceContext.signature(method),
            owner,source?origin:"",declaration==null?0:SourceContext.line(declaration),origin,
            !owner.isEmpty() && (!source || origins.analyzed(origin)));
        return id;
    }
    private ITypeBinding enclosingType(ASTNode n) {
        for(ASTNode p=n.getParent();p!=null;p=p.getParent()) {
            if(p instanceof AbstractTypeDeclaration t)return t.resolveBinding();
            if(p instanceof AnonymousClassDeclaration a)return a.resolveBinding();
        }
        return null;
    }
    String caller(ASTNode node) {
        for(ASTNode p=node instanceof EnumConstantDeclaration?node:node.getParent();p!=null;p=p.getParent()) {
            if(p instanceof MethodDeclaration m)return methodIdentity(m.resolveBinding(),m);
            if(p instanceof LambdaExpression || p instanceof Initializer || p instanceof FieldDeclaration || p instanceof EnumConstantDeclaration) {
                String file=AuditOrigins.canonical(SourceContext.file()),name=SourceContext.owner(node);
                String id=file+"#"+name;
                String owner=typeIdentity(enclosingType(p),null);
                identity(id,name,p instanceof LambdaExpression?"lambda":"initializer","",owner,file,
                    SourceContext.line(p),file,!owner.isEmpty());return id;
            }
        }
        return "";
    }
    private void call(ASTNode node,IMethodBinding method,String kind) {
        String caller=caller(node),target=methodIdentity(method,null);
        boolean resolved=!caller.isEmpty() && !target.isEmpty();
        JsonObject ref=new JsonObject();ref.addProperty("caller",caller);ref.addProperty("target",target);
        ref.addProperty("file",SourceContext.file());ref.addProperty("line",SourceContext.line(node));
        ref.addProperty("resolved",resolved);ref.addProperty("kind",kind);
        ref.addProperty("dependency",method==null?"":origins.dependencyId(method.getDeclaringClass()));
        boolean direct=method!=null && (method.isConstructor() || Modifier.isStatic(method.getModifiers()) ||
            Modifier.isPrivate(method.getModifiers()) || Modifier.isFinal(method.getModifiers()) ||
            method.getDeclaringClass()!=null && Modifier.isFinal(method.getDeclaringClass().getModifiers()) ||
            node instanceof SuperMethodInvocation);
        ref.addProperty("dispatch",direct?"direct":"virtual");ref.addProperty("runtimeTargetsComplete",direct);
        references.add(ref);
        if(!resolved)unknown(node,"unresolved Java declaration origin or owner: "+node);
        if(method!=null && method.getDeclaringClass()!=null && method.getDeclaringClass().isFromSource()) {
            String origin=origins.origin(method.getDeclaringClass());
            if(!origin.isEmpty() && !origins.analyzed(origin))
                unknown(node,"source declaration is outside the analyzed file set: "+origin);
        }
        if(node instanceof MethodInvocation invocation) {
            JsonObject nativeCall=nativeResolver.call(invocation);
            if(nativeCall!=null) {
                nativeCall.addProperty("caller",caller);nativeCall.addProperty("file",SourceContext.file());
                nativeCall.addProperty("line",SourceContext.line(node));nativeBindings.add(nativeCall);
            }
        }
    }
    void collect(CompilationUnit unit) {
        unit.accept(new ASTVisitor() {
            @Override public void preVisit(ASTNode n) {
                if(n instanceof AbstractTypeDeclaration t)typeIdentity(t.resolveBinding(),t);
                if(n instanceof AnonymousClassDeclaration a)typeIdentity(a.resolveBinding(),a);
            }
            @Override public boolean visit(MethodDeclaration n) {methodIdentity(n.resolveBinding(),n);return true;}
            @Override public boolean visit(MethodInvocation n) {call(n,n.resolveMethodBinding(),"invocation");return true;}
            @Override public boolean visit(SuperMethodInvocation n) {call(n,n.resolveMethodBinding(),"super-invocation");return true;}
            @Override public boolean visit(ClassInstanceCreation n) {call(n,n.resolveConstructorBinding(),"construction");return true;}
            @Override public boolean visit(ConstructorInvocation n) {call(n,n.resolveConstructorBinding(),"constructor-invocation");return true;}
            @Override public boolean visit(SuperConstructorInvocation n) {call(n,n.resolveConstructorBinding(),"super-constructor-invocation");return true;}
            @Override public boolean visit(EnumConstantDeclaration n) {call(n,n.resolveConstructorBinding(),"enum-construction");return true;}
            @Override public boolean visit(ExpressionMethodReference n) {call(n,n.resolveMethodBinding(),"method-reference");return true;}
            @Override public boolean visit(TypeMethodReference n) {call(n,n.resolveMethodBinding(),"method-reference");return true;}
            @Override public boolean visit(SuperMethodReference n) {call(n,n.resolveMethodBinding(),"method-reference");return true;}
            @Override public boolean visit(CreationReference n) {call(n,n.resolveMethodBinding(),"constructor-reference");return true;}
        });
    }
    JsonObject json(boolean analysisComplete) {
        unresolved.addAll(origins.problems());
        JsonObject out=new JsonObject();out.addProperty("schemaVersion",1);out.addProperty("provider","java-jdt-3.44");
        out.addProperty("supported",true);out.addProperty("complete",analysisComplete && unresolved.isEmpty());
        JsonArray ids=new JsonArray();identities.values().forEach(ids::add);out.add("identities",ids);
        out.add("dependencies",origins.dependencies());out.add("references",references);out.add("nativeBindings",nativeBindings);
        out.add("inputs",origins.inputs());JsonArray missing=new JsonArray();unresolved.forEach(missing::add);out.add("unresolved",missing);
        return out;
    }
}
