package topo.extract;

import com.google.gson.*;
import org.eclipse.jdt.core.dom.*;
import java.util.Arrays;
import java.util.stream.Collectors;

/** Per compilation-unit source identity; offsets are UTF-16, end exclusive. */
final class SourceContext {
    private static String file, source;
    private static CompilationUnit unit;
    static void enter(String f, String s, CompilationUnit u) { file=f; source=s; unit=u; }
    static JsonObject span(ASTNode n) {
        JsonObject s = new JsonObject();
        int begin=n.getStartPosition(), end=begin+n.getLength();
        s.addProperty("file", file); s.addProperty("startOffset",begin); s.addProperty("endOffset",end);
        s.addProperty("startLine",unit.getLineNumber(begin)); s.addProperty("startColumn",unit.getColumnNumber(begin)+1);
        // JDT returns -1 for the EOF position. Derive the exclusive end from
        // the last occupied UTF-16 code unit in that case.
        int line=unit.getLineNumber(end), col=unit.getColumnNumber(end)+1;
        if (line < 0 && end > 0) { line=unit.getLineNumber(end-1); col=unit.getColumnNumber(end-1)+2; }
        s.addProperty("endLine",line); s.addProperty("endColumn",col); return s;
    }
    static JsonObject attach(JsonObject json, ASTNode n) {
        if (unit == null || n == null) return json;
        json.add("sourceSpan",span(n));
        int start=n.getStartPosition(), end=start+n.getLength();
        if (start >= 0 && end <= source.length()) json.addProperty("rawSource", source.substring(start,end));
        return json;
    }
    static String file() { return file; }
    static String owner(ASTNode n) {
        for (ASTNode p=n instanceof EnumConstantDeclaration?n:n.getParent();p!=null;p=p.getParent()) {
            if (p instanceof LambdaExpression)
                return enclosingType(p) + ".<lambda>@" + p.getStartPosition();
            if (p instanceof MethodDeclaration m)
                return m.resolveBinding()==null ? enclosingType(m)+"."+m.getName() : qualified(m.resolveBinding());
            if (p instanceof Initializer i)
                return enclosingType(i)+(Modifier.isStatic(i.getModifiers())?".<clinit>@":".<init>@")+i.getStartPosition();
            if (p instanceof FieldDeclaration f)
                return enclosingType(f)+(Modifier.isStatic(f.getModifiers())?".<clinit>@":".<init>@")+f.getStartPosition();
            if (p instanceof EnumConstantDeclaration)
                return enclosingType(p)+".<clinit>@"+p.getStartPosition();
        }
        return "";
    }
    static String enclosingType(ASTNode n) {
        for (ASTNode p=n.getParent();p!=null;p=p.getParent()) {
            ITypeBinding b=p instanceof AbstractTypeDeclaration d ? d.resolveBinding()
                : p instanceof AnonymousClassDeclaration a ? a.resolveBinding() : null;
            if(b!=null) {
                String name=b.getTypeDeclaration().getQualifiedName();
                return name.isEmpty()?b.getBinaryName():name;
            }
        }
        return "<unknown>";
    }
    static int line(ASTNode n) { return unit.getLineNumber(n.getStartPosition()); }
    static String qualified(IMethodBinding b) {
        if (b == null || b.getDeclaringClass() == null) return "";
        return b.getDeclaringClass().getErasure().getQualifiedName()+"."+(b.isConstructor() ? b.getDeclaringClass().getName() : b.getName());
    }
    static String signature(IMethodBinding b) {
        if (b == null) return "";
        return qualified(b)+"("+Arrays.stream(b.getParameterTypes()).map(ITypeBinding::getQualifiedName).collect(Collectors.joining(","))+"):"+b.getReturnType().getQualifiedName();
    }
    static void binding(JsonObject out, IMethodBinding b) {
        boolean resolved=b != null && !b.isRecovered() && b.getDeclaringClass()!=null && !b.getDeclaringClass().isRecovered();
        out.addProperty("bindingResolved",resolved);
        if (resolved) { out.addProperty("resolvedQualifiedName",qualified(b)); out.addProperty("resolvedSignature",signature(b)); }
    }
    static JsonObject type(ITypeBinding t) {
        JsonObject out=new JsonObject(); JsonArray parts=new JsonArray();
        parts.add(t == null ? "unknown" : t.getQualifiedName()); out.add("nameParts",parts); return out;
    }
}
