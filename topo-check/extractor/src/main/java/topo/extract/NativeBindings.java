package topo.extract;

import com.google.gson.JsonObject;
import org.eclipse.jdt.core.dom.*;
import java.util.*;

/** Bounded expression provenance for FFM; deliberately does not evaluate Java. */
final class NativeBindings {
    private record Value(String kind,String library,String symbol,String descriptor,String callback,String lookup,String reason) {
        static Value scalar(String kind,String text) {return new Value(kind,text,"","","","","");}
        static Value unknown(String reason) {return new Value("unknown","","","","","",reason);}
        boolean known() {return reason.isEmpty();}
    }
    private final Map<String,Expression> definitions=new HashMap<>();
    private final Set<String> writes=new HashSet<>();
    private final JavaAuditFacts audit;
    NativeBindings(Map<String,CompilationUnit> units,JavaAuditFacts audit) {
        this.audit=audit;
        for(CompilationUnit unit:units.values())unit.accept(new ASTVisitor() {
            @Override public boolean visit(VariableDeclarationFragment n) {
                IVariableBinding b=n.resolveBinding();
                if(b!=null && n.getInitializer()!=null && (!b.isField() || Modifier.isFinal(b.getModifiers())))
                    definitions.put(b.getVariableDeclaration().getKey(),n.getInitializer());
                return true;
            }
            @Override public boolean visit(Assignment n) {write(n.getLeftHandSide());return true;}
            @Override public boolean visit(PostfixExpression n) {write(n.getOperand());return true;}
            @Override public boolean visit(PrefixExpression n) {
                if(n.getOperator()==PrefixExpression.Operator.INCREMENT || n.getOperator()==PrefixExpression.Operator.DECREMENT)write(n.getOperand());
                return true;
            }
            private void write(Expression e) {IVariableBinding b=variable(e);if(b!=null)writes.add(b.getVariableDeclaration().getKey());}
        });
    }
    private static IVariableBinding variable(Expression e) {
        IBinding binding=e instanceof Name n?n.resolveBinding():e instanceof FieldAccess f?f.resolveFieldBinding():null;
        return binding instanceof IVariableBinding v?v:null;
    }
    private static String owner(IMethodBinding b) {
        return b==null || b.getDeclaringClass()==null?"":b.getDeclaringClass().getErasure().getQualifiedName();
    }
    private Value evaluate(Expression expression) {return evaluate(expression,new HashSet<>(),0);}
    private Value evaluate(Expression e,Set<String> active,int depth) {
        if(e==null)return Value.unknown("missing receiver or argument");
        if(depth>64)return Value.unknown("FFM static expression depth exceeds 64");
        if(e instanceof ParenthesizedExpression p)return evaluate(p.getExpression(),active,depth+1);
        if(e instanceof CastExpression c)return evaluate(c.getExpression(),active,depth+1);
        Object constant=e.resolveConstantExpressionValue();
        if(constant instanceof String text)return Value.scalar("string",text);
        IVariableBinding variable=variable(e);
        if(variable!=null) {
            ITypeBinding declaring=variable.getDeclaringClass();
            if(declaring!=null && declaring.getQualifiedName().startsWith("java.lang.foreign.ValueLayout") &&
               Modifier.isStatic(variable.getModifiers()) && Modifier.isFinal(variable.getModifiers()))
                return Value.scalar("layout",variable.getName());
            String key=variable.getVariableDeclaration().getKey();
            if(writes.contains(key))return Value.unknown("FFM value has multiple or mutable assignments: "+variable.getName());
            if(!active.add(key))return Value.unknown("cyclic FFM value definition: "+variable.getName());
            Expression init=definitions.get(key);
            Value out=init==null?Value.unknown("FFM value has no supported immutable definition: "+variable.getName()):evaluate(init,active,depth+1);
            active.remove(key);return out;
        }
        if(!(e instanceof MethodInvocation call))return Value.unknown("unsupported FFM expression: "+e.getClass().getSimpleName());
        IMethodBinding binding=call.resolveMethodBinding();String owner=owner(binding),name=call.getName().getIdentifier();
        List<?> args=call.arguments();
        if(owner.equals("java.nio.file.Path") && name.equals("of") || owner.equals("java.nio.file.Paths") && name.equals("get")) {
            List<String> parts=new ArrayList<>();
            for(Object arg:args) {Value part=evaluate((Expression)arg,active,depth+1);if(!part.kind.equals("string"))return Value.unknown("library path is not a constant string");parts.add(part.library);}
            if(parts.isEmpty())return Value.unknown("missing library path");
            try {return Value.scalar("path",java.nio.file.Path.of(parts.getFirst(),parts.subList(1,parts.size()).toArray(String[]::new)).normalize().toString());}
            catch(RuntimeException ex) {return Value.unknown("invalid static library path");}
        }
        if(owner.equals("java.lang.foreign.SymbolLookup")) {
            if(name.equals("libraryLookup") && !args.isEmpty()) {
                Value path=evaluate((Expression)args.getFirst(),active,depth+1);
                if(!path.kind.equals("path") && !path.kind.equals("string"))return Value.unknown("native library path/name is dynamic");
                return new Value("lookup",path.library,"","","",path.kind.equals("path")?"library-path":"library-name","");
            }
            if(name.equals("loaderLookup"))return new Value("lookup","","","","","loader","loader lookup has no fixed native library");
            if(name.equals("find") && args.size()==1) {
                Value lookup=evaluate(call.getExpression(),active,depth+1),symbol=evaluate((Expression)args.getFirst(),active,depth+1);
                String reason=!lookup.known()?lookup.reason:!lookup.kind.equals("lookup")?"unresolved symbol lookup origin":!symbol.kind.equals("string")?"native symbol name is dynamic":"";
                return new Value("symbol",lookup.library,symbol.kind.equals("string")?symbol.library:"","","",lookup.lookup,reason);
            }
        }
        if(owner.equals("java.util.Optional") && (name.equals("orElseThrow") || name.equals("get")))
            return evaluate(call.getExpression(),active,depth+1);
        if(owner.equals("java.lang.foreign.FunctionDescriptor") && (name.equals("of") || name.equals("ofVoid"))) {
            List<String> layouts=new ArrayList<>();
            for(Object arg:args) {Value layout=evaluate((Expression)arg,active,depth+1);if(!layout.kind.equals("layout"))return Value.unknown("FFM descriptor contains an unsupported/dynamic layout");layouts.add(layout.library);}
            String result=name.equals("ofVoid")?"void":layouts.isEmpty()?"":layouts.removeFirst();
            if(result.isEmpty())return Value.unknown("missing FFM return layout");
            return new Value("descriptor","","",result+"("+String.join(",",layouts)+")","","","");
        }
        if(owner.equals("java.lang.invoke.MethodHandles.Lookup") && name.equals("findStatic") && args.size()==3)
            return callback(call,active,depth);
        if(owner.equals("java.lang.foreign.Linker")) {
            if(name.equals("defaultLookup"))return new Value("lookup","","","","","default","default lookup has no fixed native library");
            if(name.equals("nativeLinker"))return Value.scalar("linker","");
            if(name.equals("downcallHandle")) {
                Value address=args.isEmpty()?Value.unknown("missing native address"):evaluate((Expression)args.getFirst(),active,depth+1);
                Value descriptor=args.size()<2?Value.unknown("unbound/dynamic native address"):evaluate((Expression)args.get(1),active,depth+1);
                String reason=!address.known()?address.reason:!address.kind.equals("symbol")?"native address is not a resolved lookup symbol":!descriptor.kind.equals("descriptor")?descriptor.reason:args.size()>2?"FFM linker options are outside the supported static chain":"";
                return new Value("downcall",address.library,address.symbol,descriptor.descriptor,"",address.lookup,reason);
            }
            if(name.equals("upcallStub")) {
                Value target=args.isEmpty()?Value.unknown("missing upcall target"):evaluate((Expression)args.getFirst(),active,depth+1);
                Value descriptor=args.size()<2?Value.unknown("missing upcall descriptor"):evaluate((Expression)args.get(1),active,depth+1);
                String reason=!target.kind.equals("callback")?target.reason:!descriptor.kind.equals("descriptor")?descriptor.reason:args.size()>3?"FFM upcall options are outside the supported static chain":"";
                if(reason.isEmpty() && target.callback.isEmpty())reason="unresolved upcall callback identity";
                return new Value("upcall","","",descriptor.descriptor,target.callback,"upcall",reason);
            }
        }
        return Value.unknown("unsupported/dynamic FFM value: "+owner+"."+name);
    }
    private Value callback(MethodInvocation call,Set<String> active,int depth) {
        List<?> args=call.arguments();
        if(!(args.get(0) instanceof TypeLiteral target))return Value.unknown("upcall target class is dynamic");
        Value methodName=evaluate((Expression)args.get(1),active,depth+1);
        if(!methodName.kind.equals("string"))return Value.unknown("upcall method name is dynamic");
        if(!(args.get(2) instanceof MethodInvocation methodType) || !owner(methodType.resolveMethodBinding()).equals("java.lang.invoke.MethodType") || !methodType.getName().getIdentifier().equals("methodType"))
            return Value.unknown("upcall method type is outside the supported static chain");
        List<String> signature=new ArrayList<>();
        for(Object arg:methodType.arguments()) {
            if(!(arg instanceof TypeLiteral literal) || literal.getType().resolveBinding()==null)return Value.unknown("upcall method type is dynamic");
            signature.add(literal.getType().resolveBinding().getQualifiedName());
        }
        ITypeBinding type=target.getType().resolveBinding();
        if(type==null || signature.isEmpty())return Value.unknown("upcall class/type binding is unresolved");
        for(IMethodBinding method:type.getDeclaredMethods()) {
            if(!method.getName().equals(methodName.library) || !Modifier.isStatic(method.getModifiers()) || !method.getReturnType().getQualifiedName().equals(signature.getFirst()) || method.getParameterTypes().length!=signature.size()-1)continue;
            boolean equal=true;for(int i=0;i<method.getParameterTypes().length;i++)equal&=method.getParameterTypes()[i].getQualifiedName().equals(signature.get(i+1));
            if(equal) {String id=audit.methodIdentity(method,null);return new Value("callback","","","",id,"",id.isEmpty()?"unresolved upcall callback origin":"");}
        }
        return Value.unknown("upcall static method does not match its MethodType");
    }
    JsonObject call(MethodInvocation call) {
        IMethodBinding binding=call.resolveMethodBinding();String owner=owner(binding),name=call.getName().getIdentifier();
        Value value;
        if(owner.equals("java.lang.invoke.MethodHandle") && (name.equals("invokeExact") || name.equals("invoke"))) {
            value=evaluate(call.getExpression());
            if(!value.kind.equals("downcall"))value=Value.unknown("method-handle target/address is not a resolved static downcall chain");
        } else if(owner.startsWith("java.lang.foreign.")) {
            if(owner.equals("java.lang.foreign.FunctionDescriptor") || owner.startsWith("java.lang.foreign.ValueLayout") || owner.startsWith("java.lang.foreign.MemoryLayout"))return null;
            value=evaluate(call);
            // Allocation/lifetime capabilities have a statically bound API,
            // without claiming a native library/address identity.
            if(owner.equals("java.lang.foreign.Arena") || owner.equals("java.lang.foreign.Linker") && name.equals("nativeLinker"))
                value=new Value("capability","","","","","","");
        } else return null;
        JsonObject out=new JsonObject();out.addProperty("operation",owner+"."+name);
        out.addProperty("library",value.library);out.addProperty("symbol",value.symbol);out.addProperty("descriptor",value.descriptor);
        out.addProperty("callback",value.callback);out.addProperty("lookup",value.lookup);
        out.addProperty("resolved",value.known());out.addProperty("unresolvedReason",value.known()?"":value.reason);
        return out;
    }
}
