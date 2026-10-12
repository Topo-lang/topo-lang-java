package topo.extract;

import com.google.gson.*;
import org.eclipse.jdt.core.dom.*;
import java.util.*;

/** Source facts remain available when a node has no portable code representation. */
final class SourceFacts {
    private final JsonArray symbols=new JsonArray(), calls=new JsonArray(), accesses=new JsonArray(), diagnostics;
    private final Set<String> unresolved=new HashSet<>();
    SourceFacts(JsonArray diagnostics) { this.diagnostics=diagnostics; }
    JsonObject json() { JsonObject j=new JsonObject();j.add("symbols",symbols);j.add("calls",calls);j.add("accesses",accesses);return j; }
    private static String owner(ASTNode n) {
        for (ASTNode p=n.getParent();p!=null;p=p.getParent()) if (p instanceof MethodDeclaration m)
            return m.resolveBinding()==null ? m.getName().getIdentifier() : SourceContext.qualified(m.resolveBinding());
        return "";
    }
    private JsonObject location(ASTNode n) {
        JsonObject out=SourceContext.attach(new JsonObject(),n);
        out.addProperty("file",SourceContext.file());out.addProperty("line",SourceContext.line(n));return out;
    }
    private void call(ASTNode n, IMethodBinding b, String kind) {
        JsonObject out=location(n);String caller=owner(n);
        out.addProperty("caller",caller);out.addProperty("kind",kind);SourceContext.binding(out,b);
        if (!out.get("bindingResolved").getAsBoolean()) {
            String key=SourceContext.file()+":"+n.getStartPosition();
            if(unresolved.add(key)) Main.diagnostic(diagnostics,"error","unresolved-call","Unresolved call: "+n,SourceContext.file(),SourceContext.line(n));
        } else {
            out.addProperty("callee",SourceContext.qualified(b));out.addProperty("external",!b.getDeclaringClass().isFromSource());
            out.addProperty("dispatch",Modifier.isStatic(b.getModifiers()) ? "static" : b.isConstructor() ? "constructor" : "virtual");
            // This is a declaration-resolved graph, not a closed-world target proof.
            out.addProperty("runtimeTargetsComplete",Modifier.isStatic(b.getModifiers()) || b.isConstructor() || Modifier.isFinal(b.getModifiers()) || Modifier.isPrivate(b.getModifiers()));
        }
        if (!caller.isEmpty()) calls.add(out);
    }
    void collect(CompilationUnit unit) {
        unit.accept(new ASTVisitor() {
            @Override public void preVisit(ASTNode n) {
                if (n instanceof AbstractTypeDeclaration type) {
                    JsonObject out=location(n);ITypeBinding b=type.resolveBinding();String name=type.getName().getIdentifier();
                    out.addProperty("qualifiedName",b==null?name:b.getQualifiedName());out.addProperty("simpleName",name);
                    out.addProperty("kind",type instanceof EnumDeclaration?"enum":type instanceof TypeDeclaration t&&t.isInterface()?"interface":"class");
                    visibility(out,type.getModifiers());symbols.add(out);
                }
            }
            @Override public boolean visit(MethodDeclaration n) {
                JsonObject out=location(n);IMethodBinding b=n.resolveBinding();
                out.addProperty("qualifiedName",b==null?n.getName().getIdentifier():SourceContext.qualified(b));
                out.addProperty("simpleName",n.getName().getIdentifier());out.addProperty("kind",n.isConstructor()?"constructor":"method");
                out.addProperty("isStatic",Modifier.isStatic(n.getModifiers()));visibility(out,n.getModifiers());SourceContext.binding(out,b);
                if(b!=null) {out.addProperty("returnType",b.getReturnType().getQualifiedName());out.addProperty("enclosingClass",b.getDeclaringClass().getQualifiedName());
                    JsonArray params=new JsonArray();for(ITypeBinding p:b.getParameterTypes())params.add(p.getQualifiedName());out.add("paramTypes",params);}
                symbols.add(out);return true;
            }
            @Override public boolean visit(MethodInvocation n) {call(n,n.resolveMethodBinding(),"invocation");return true;}
            @Override public boolean visit(SuperMethodInvocation n) {call(n,n.resolveMethodBinding(),"super-invocation");return true;}
            @Override public boolean visit(ClassInstanceCreation n) {call(n,n.resolveConstructorBinding(),"construction");return true;}
            @Override public boolean visit(ConstructorInvocation n) {call(n,n.resolveConstructorBinding(),"constructor-invocation");return true;}
            @Override public boolean visit(SuperConstructorInvocation n) {call(n,n.resolveConstructorBinding(),"super-constructor-invocation");return true;}
            @Override public boolean visit(ExpressionMethodReference n) {call(n,n.resolveMethodBinding(),"method-reference");return true;}
            @Override public boolean visit(TypeMethodReference n) {call(n,n.resolveMethodBinding(),"method-reference");return true;}
            @Override public boolean visit(SuperMethodReference n) {call(n,n.resolveMethodBinding(),"method-reference");return true;}
            @Override public boolean visit(CreationReference n) {call(n,n.resolveMethodBinding(),"constructor-reference");return true;}
            @Override public boolean visit(SimpleName n) {
                if(n.isDeclaration())return true;
                if (n.resolveBinding() instanceof IVariableBinding v && v.isField() && !owner(n).isEmpty() && v.getDeclaringClass()!=null) {
                    JsonObject out=location(n);out.addProperty("function",owner(n));out.addProperty("symbol",v.getDeclaringClass().getQualifiedName()+"."+v.getName());
                    ASTNode target=n;
                    while(target.getParent() instanceof QualifiedName || target.getParent() instanceof FieldAccess)target=target.getParent();
                    ASTNode p=target.getParent();
                    boolean write=p instanceof Assignment a && a.getLeftHandSide()==target || p instanceof PostfixExpression
                        || p instanceof PrefixExpression e && (e.getOperator()==PrefixExpression.Operator.INCREMENT || e.getOperator()==PrefixExpression.Operator.DECREMENT);
                    out.addProperty("isWrite",write);accesses.add(out);
                }
                return true;
            }
        });
    }
    private static void visibility(JsonObject out,int flags) {
        out.addProperty("visibility",Modifier.isPublic(flags)?"public":Modifier.isProtected(flags)?"protected":Modifier.isPrivate(flags)?"private":"internal");
    }
}
