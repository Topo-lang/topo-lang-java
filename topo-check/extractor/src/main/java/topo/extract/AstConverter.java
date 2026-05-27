// AstConverter: Converts Eclipse JDT AST nodes to TranspileModel JSON objects.
//
// Each convert* method returns a JsonObject with a "kind" field matching the
// TranspileModel node types defined in topo-core/include/topo/Transpile/TranspileModel.h.

package topo.extract;

import com.google.gson.JsonArray;
import com.google.gson.JsonNull;
import com.google.gson.JsonObject;
import org.eclipse.jdt.core.dom.*;

import java.util.List;

public final class AstConverter {

    private AstConverter() {}

    // -----------------------------------------------------------------------
    // Type conversion
    // -----------------------------------------------------------------------

    /**
     * Convert a JDT Type node to a TypeNode JSON object with "nameParts" array.
     */
    public static JsonObject convertType(Type type) {
        JsonObject node = new JsonObject();
        JsonArray parts = new JsonArray();

        if (type == null) {
            parts.add("void");
        } else if (type instanceof PrimitiveType pt) {
            parts.add(pt.getPrimitiveTypeCode().toString());
        } else if (type instanceof SimpleType st) {
            parts.add(st.getName().getFullyQualifiedName());
        } else if (type instanceof QualifiedType qt) {
            parts.add(qt.getQualifier().toString());
            parts.add(qt.getName().getIdentifier());
        } else if (type instanceof ArrayType at) {
            JsonObject elemType = convertType(at.getElementType());
            JsonArray elemParts = elemType.getAsJsonArray("nameParts");
            for (var el : elemParts) {
                parts.add(el);
            }
            // Append [] for each dimension
            for (int i = 0; i < at.getDimensions(); i++) {
                String last = parts.get(parts.size() - 1).getAsString();
                parts.set(parts.size() - 1, new com.google.gson.JsonPrimitive(last + "[]"));
            }
        } else if (type instanceof ParameterizedType pt) {
            JsonObject rawType = convertType(pt.getType());
            JsonArray rawParts = rawType.getAsJsonArray("nameParts");
            for (var el : rawParts) {
                parts.add(el);
            }
        } else {
            parts.add(type.toString());
        }

        node.add("nameParts", parts);
        return node;
    }

    // -----------------------------------------------------------------------
    // Statement conversion
    // -----------------------------------------------------------------------

    public static JsonObject convertStatement(Statement stmt) {
        if (stmt instanceof VariableDeclarationStatement vds) {
            return convertVarDeclStmt(vds);
        } else if (stmt instanceof ExpressionStatement es) {
            return convertExprStmt(es);
        } else if (stmt instanceof ReturnStatement rs) {
            return convertReturnStmt(rs);
        } else if (stmt instanceof IfStatement is) {
            return convertIfStmt(is);
        } else if (stmt instanceof ForStatement fs) {
            return convertForStmt(fs);
        } else if (stmt instanceof EnhancedForStatement efs) {
            return convertEnhancedForStmt(efs);
        } else if (stmt instanceof WhileStatement ws) {
            return convertWhileStmt(ws);
        } else if (stmt instanceof Block block) {
            return convertBlockStmt(block);
        } else if (stmt instanceof ThrowStatement) {
            return unsupported("throw statement");
        } else if (stmt instanceof TryStatement) {
            return unsupported("try statement");
        } else if (stmt instanceof SwitchStatement) {
            return unsupported("switch statement");
        } else if (stmt instanceof DoStatement ds) {
            return convertDoWhileStmt(ds);
        } else if (stmt instanceof BreakStatement) {
            JsonObject obj = new JsonObject();
            obj.addProperty("kind", "BreakStmt");
            return obj;
        } else if (stmt instanceof ContinueStatement) {
            JsonObject obj = new JsonObject();
            obj.addProperty("kind", "ContinueStmt");
            return obj;
        } else {
            return unsupported("unhandled statement: " + stmt.getClass().getSimpleName());
        }
    }

    private static JsonObject convertVarDeclStmt(VariableDeclarationStatement vds) {
        // May have multiple fragments; emit one VarDeclStmt per fragment.
        // For simplicity, return the first one.
        @SuppressWarnings("unchecked")
        List<VariableDeclarationFragment> frags = vds.fragments();
        if (frags.isEmpty()) {
            return unsupported("empty variable declaration");
        }

        VariableDeclarationFragment frag = frags.get(0);
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "VarDeclStmt");
        obj.addProperty("name", frag.getName().getIdentifier());
        obj.add("type", convertType(vds.getType()));
        if (frag.getInitializer() != null) {
            obj.add("init", convertExpression(frag.getInitializer()));
        } else {
            obj.add("init", JsonNull.INSTANCE);
        }
        return obj;
    }

    private static JsonObject convertExprStmt(ExpressionStatement es) {
        Expression expr = es.getExpression();
        // Assignment is an expression in Java
        if (expr instanceof Assignment assign) {
            return convertAssignment(assign);
        }
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "ExprStmt");
        obj.add("expr", convertExpression(expr));
        return obj;
    }

    private static JsonObject convertReturnStmt(ReturnStatement rs) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "ReturnStmt");
        if (rs.getExpression() != null) {
            obj.add("value", convertExpression(rs.getExpression()));
        } else {
            obj.add("value", JsonNull.INSTANCE);
        }
        return obj;
    }

    private static JsonObject convertIfStmt(IfStatement is) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "IfStmt");
        obj.add("condition", convertExpression(is.getExpression()));
        obj.add("thenBody", statementsToArray(is.getThenStatement()));
        if (is.getElseStatement() != null) {
            obj.add("elseBody", statementsToArray(is.getElseStatement()));
        } else {
            obj.add("elseBody", JsonNull.INSTANCE);
        }
        return obj;
    }

    private static JsonObject convertForStmt(ForStatement fs) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "ForStmt");

        // Initializers
        @SuppressWarnings("unchecked")
        List<Expression> inits = fs.initializers();
        if (!inits.isEmpty()) {
            obj.add("init", convertExpression(inits.get(0)));
        }

        if (fs.getExpression() != null) {
            obj.add("condition", convertExpression(fs.getExpression()));
        }

        // Updaters
        @SuppressWarnings("unchecked")
        List<Expression> updaters = fs.updaters();
        if (!updaters.isEmpty()) {
            obj.add("update", convertExpression(updaters.get(0)));
        }

        obj.add("body", statementsToArray(fs.getBody()));
        return obj;
    }

    private static JsonObject convertEnhancedForStmt(EnhancedForStatement efs) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "ForStmt");
        obj.addProperty("variable", efs.getParameter().getName().getIdentifier());
        obj.add("iterable", convertExpression(efs.getExpression()));
        obj.add("body", statementsToArray(efs.getBody()));
        return obj;
    }

    private static JsonObject convertWhileStmt(WhileStatement ws) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "WhileStmt");
        obj.add("condition", convertExpression(ws.getExpression()));
        obj.add("body", statementsToArray(ws.getBody()));
        return obj;
    }

    private static JsonObject convertDoWhileStmt(DoStatement ds) {
        // Approximate as WhileStmt with a note
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "WhileStmt");
        obj.add("condition", convertExpression(ds.getExpression()));
        obj.add("body", statementsToArray(ds.getBody()));
        obj.addProperty("doWhile", true);
        return obj;
    }

    private static JsonObject convertBlockStmt(Block block) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "BlockExpr");
        obj.add("body", statementsToArray(block));
        return obj;
    }

    private static JsonObject convertAssignment(Assignment assign) {
        JsonObject obj = new JsonObject();
        if (assign.getOperator() == Assignment.Operator.ASSIGN) {
            obj.addProperty("kind", "AssignStmt");
            obj.add("target", convertExpression(assign.getLeftHandSide()));
            obj.add("value", convertExpression(assign.getRightHandSide()));
        } else {
            // Compound assignments (+=, -=, etc.) -> BinaryOpExpr wrapped in AssignStmt
            obj.addProperty("kind", "AssignStmt");
            obj.add("target", convertExpression(assign.getLeftHandSide()));

            JsonObject binOp = new JsonObject();
            binOp.addProperty("kind", "BinaryOpExpr");
            binOp.addProperty("op", compoundAssignOp(assign.getOperator()));
            binOp.add("lhs", convertExpression(assign.getLeftHandSide()));
            binOp.add("rhs", convertExpression(assign.getRightHandSide()));
            obj.add("value", binOp);
        }
        return obj;
    }

    // -----------------------------------------------------------------------
    // Expression conversion
    // -----------------------------------------------------------------------

    public static JsonObject convertExpression(Expression expr) {
        if (expr instanceof InfixExpression ie) {
            return convertInfix(ie);
        } else if (expr instanceof PrefixExpression pe) {
            return convertPrefix(pe);
        } else if (expr instanceof PostfixExpression pfe) {
            return convertPostfix(pfe);
        } else if (expr instanceof MethodInvocation mi) {
            return convertMethodInvocation(mi);
        } else if (expr instanceof FieldAccess fa) {
            return convertFieldAccess(fa);
        } else if (expr instanceof QualifiedName qn) {
            return convertQualifiedName(qn);
        } else if (expr instanceof ArrayAccess aa) {
            return convertArrayAccess(aa);
        } else if (expr instanceof NumberLiteral nl) {
            return convertNumberLiteral(nl);
        } else if (expr instanceof StringLiteral sl) {
            return convertStringLiteral(sl);
        } else if (expr instanceof BooleanLiteral bl) {
            return convertBooleanLiteral(bl);
        } else if (expr instanceof CharacterLiteral cl) {
            return convertCharLiteral(cl);
        } else if (expr instanceof NullLiteral) {
            return convertNullLiteral();
        } else if (expr instanceof SimpleName sn) {
            return convertSimpleName(sn);
        } else if (expr instanceof ClassInstanceCreation cic) {
            return convertClassInstanceCreation(cic);
        } else if (expr instanceof ThisExpression) {
            JsonObject obj = new JsonObject();
            obj.addProperty("kind", "VarRefExpr");
            obj.addProperty("name", "this");
            return obj;
        } else if (expr instanceof ParenthesizedExpression pe) {
            return convertExpression(pe.getExpression());
        } else if (expr instanceof CastExpression ce) {
            JsonObject obj = new JsonObject();
            obj.addProperty("kind", "CastExpr");
            obj.add("expr", convertExpression(ce.getExpression()));
            obj.add("targetType", convertType(ce.getType()));
            return obj;
        } else if (expr instanceof ConditionalExpression ce) {
            return convertConditional(ce);
        } else if (expr instanceof ArrayCreation) {
            return unsupported("array creation expression");
        } else if (expr instanceof ArrayInitializer) {
            return unsupported("array initializer expression");
        } else if (expr instanceof InstanceofExpression) {
            return unsupported("instanceof expression");
        } else if (expr instanceof LambdaExpression) {
            return unsupported("lambda expression");
        } else if (expr instanceof MethodReference) {
            return unsupported("method reference expression");
        } else if (expr instanceof SuperMethodInvocation) {
            return unsupported("super method invocation");
        } else if (expr instanceof Assignment assign) {
            return convertAssignment(assign);
        } else {
            return unsupported("unhandled expression: " + expr.getClass().getSimpleName());
        }
    }

    private static JsonObject convertInfix(InfixExpression ie) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "BinaryOpExpr");
        obj.addProperty("op", infixOp(ie.getOperator()));
        obj.add("lhs", convertExpression(ie.getLeftOperand()));
        obj.add("rhs", convertExpression(ie.getRightOperand()));

        // Handle extended operands (e.g. a + b + c)
        @SuppressWarnings("unchecked")
        List<Expression> extended = ie.extendedOperands();
        if (!extended.isEmpty()) {
            JsonObject current = obj;
            for (Expression ext : extended) {
                JsonObject wrapper = new JsonObject();
                wrapper.addProperty("kind", "BinaryOpExpr");
                wrapper.addProperty("op", infixOp(ie.getOperator()));
                wrapper.add("lhs", current);
                wrapper.add("rhs", convertExpression(ext));
                current = wrapper;
            }
            return current;
        }
        return obj;
    }

    private static JsonObject convertPrefix(PrefixExpression pe) {
        JsonObject obj = new JsonObject();
        PrefixExpression.Operator op = pe.getOperator();
        if (op == PrefixExpression.Operator.INCREMENT || op == PrefixExpression.Operator.DECREMENT) {
            // Approximate pre-increment/decrement as compound assign
            obj.addProperty("kind", "AssignStmt");
            obj.add("target", convertExpression(pe.getOperand()));
            JsonObject binOp = new JsonObject();
            binOp.addProperty("kind", "BinaryOpExpr");
            binOp.addProperty("op", op == PrefixExpression.Operator.INCREMENT ? "+" : "-");
            binOp.add("lhs", convertExpression(pe.getOperand()));
            JsonObject one = new JsonObject();
            one.addProperty("kind", "LiteralExpr");
            one.addProperty("literalKind", "int");
            one.addProperty("value", "1");
            binOp.add("rhs", one);
            obj.add("value", binOp);
        } else {
            obj.addProperty("kind", "UnaryOpExpr");
            obj.addProperty("op", prefixOp(op));
            obj.add("operand", convertExpression(pe.getOperand()));
        }
        return obj;
    }

    private static JsonObject convertPostfix(PostfixExpression pfe) {
        // Approximate post-increment/decrement as compound assign
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "AssignStmt");
        obj.add("target", convertExpression(pfe.getOperand()));
        JsonObject binOp = new JsonObject();
        binOp.addProperty("kind", "BinaryOpExpr");
        String opStr = pfe.getOperator() == PostfixExpression.Operator.INCREMENT ? "+" : "-";
        binOp.addProperty("op", opStr);
        binOp.add("lhs", convertExpression(pfe.getOperand()));
        JsonObject one = new JsonObject();
        one.addProperty("kind", "LiteralExpr");
        one.addProperty("literalKind", "int");
        one.addProperty("value", "1");
        binOp.add("rhs", one);
        obj.add("value", binOp);
        return obj;
    }

    private static JsonObject convertMethodInvocation(MethodInvocation mi) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "CallExpr");

        if (mi.getExpression() != null) {
            // object.method(...)
            JsonObject callee = new JsonObject();
            callee.addProperty("kind", "MemberAccessExpr");
            callee.add("object", convertExpression(mi.getExpression()));
            callee.addProperty("member", mi.getName().getIdentifier());
            obj.add("callee", callee);
        } else {
            // method(...) -- unqualified
            JsonObject callee = new JsonObject();
            callee.addProperty("kind", "VarRefExpr");
            callee.addProperty("name", mi.getName().getIdentifier());
            obj.add("callee", callee);
        }

        // Issue #12: when JDT binding resolution is enabled, attach the resolved
        // qualified name so downstream consumers (containment classification,
        // transpiler) can match against catalogs by fully-qualified name rather
        // than by surface form. Falls back to absent when bindings are off.
        IMethodBinding binding = mi.resolveMethodBinding();
        if (binding != null) {
            ITypeBinding declaring = binding.getDeclaringClass();
            if (declaring != null) {
                String fqn = declaring.getQualifiedName();
                if (fqn != null && !fqn.isEmpty()) {
                    obj.addProperty("resolvedQualifiedName", fqn + "." + binding.getName());
                }
            }
        }

        JsonArray args = new JsonArray();
        @SuppressWarnings("unchecked")
        List<Expression> argList = mi.arguments();
        for (Expression arg : argList) {
            args.add(convertExpression(arg));
        }
        obj.add("args", args);
        return obj;
    }

    private static JsonObject convertFieldAccess(FieldAccess fa) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "MemberAccessExpr");
        obj.add("object", convertExpression(fa.getExpression()));
        obj.addProperty("member", fa.getName().getIdentifier());
        return obj;
    }

    private static JsonObject convertQualifiedName(QualifiedName qn) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "MemberAccessExpr");
        obj.add("object", convertExpression(qn.getQualifier()));
        obj.addProperty("member", qn.getName().getIdentifier());
        return obj;
    }

    private static JsonObject convertArrayAccess(ArrayAccess aa) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "IndexExpr");
        obj.add("object", convertExpression(aa.getArray()));
        obj.add("index", convertExpression(aa.getIndex()));
        return obj;
    }

    private static JsonObject convertNumberLiteral(NumberLiteral nl) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "LiteralExpr");
        String token = nl.getToken();
        if (token.contains(".") || token.endsWith("f") || token.endsWith("F")
                || token.endsWith("d") || token.endsWith("D")) {
            obj.addProperty("literalKind", "float");
        } else {
            obj.addProperty("literalKind", "int");
        }
        obj.addProperty("value", token);
        return obj;
    }

    private static JsonObject convertStringLiteral(StringLiteral sl) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "LiteralExpr");
        obj.addProperty("literalKind", "string");
        obj.addProperty("value", sl.getLiteralValue());
        return obj;
    }

    private static JsonObject convertBooleanLiteral(BooleanLiteral bl) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "LiteralExpr");
        obj.addProperty("literalKind", "bool");
        obj.addProperty("value", String.valueOf(bl.booleanValue()));
        return obj;
    }

    private static JsonObject convertCharLiteral(CharacterLiteral cl) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "LiteralExpr");
        obj.addProperty("literalKind", "char");
        obj.addProperty("value", String.valueOf(cl.charValue()));
        return obj;
    }

    private static JsonObject convertNullLiteral() {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "LiteralExpr");
        obj.addProperty("literalKind", "null");
        obj.addProperty("value", "null");
        return obj;
    }

    private static JsonObject convertSimpleName(SimpleName sn) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "VarRefExpr");
        obj.addProperty("name", sn.getIdentifier());
        return obj;
    }

    private static JsonObject convertClassInstanceCreation(ClassInstanceCreation cic) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "ConstructExpr");
        obj.addProperty("typeName", cic.getType().toString());

        JsonArray args = new JsonArray();
        @SuppressWarnings("unchecked")
        List<Expression> argList = cic.arguments();
        for (Expression arg : argList) {
            args.add(convertExpression(arg));
        }
        obj.add("args", args);
        return obj;
    }

    private static JsonObject convertConditional(ConditionalExpression ce) {
        // Ternary: cond ? then : else -> approximate as IfStmt
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "IfStmt");
        obj.add("condition", convertExpression(ce.getExpression()));
        JsonArray thenArr = new JsonArray();
        thenArr.add(convertExpression(ce.getThenExpression()));
        obj.add("thenBody", thenArr);
        JsonArray elseArr = new JsonArray();
        elseArr.add(convertExpression(ce.getElseExpression()));
        obj.add("elseBody", elseArr);
        return obj;
    }

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    private static JsonArray statementsToArray(Statement stmt) {
        JsonArray arr = new JsonArray();
        if (stmt instanceof Block block) {
            @SuppressWarnings("unchecked")
            List<Statement> stmts = block.statements();
            for (Statement s : stmts) {
                arr.add(convertStatement(s));
            }
        } else {
            arr.add(convertStatement(stmt));
        }
        return arr;
    }

    private static JsonObject unsupported(String description) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "UnsupportedExpr");
        obj.addProperty("description", description);
        return obj;
    }

    private static String infixOp(InfixExpression.Operator op) {
        if (op == InfixExpression.Operator.PLUS) return "+";
        if (op == InfixExpression.Operator.MINUS) return "-";
        if (op == InfixExpression.Operator.TIMES) return "*";
        if (op == InfixExpression.Operator.DIVIDE) return "/";
        if (op == InfixExpression.Operator.REMAINDER) return "%";
        if (op == InfixExpression.Operator.AND) return "&&";
        if (op == InfixExpression.Operator.OR) return "||";
        if (op == InfixExpression.Operator.XOR) return "^";
        if (op == InfixExpression.Operator.LEFT_SHIFT) return "<<";
        if (op == InfixExpression.Operator.RIGHT_SHIFT_SIGNED) return ">>";
        if (op == InfixExpression.Operator.RIGHT_SHIFT_UNSIGNED) return ">>>";
        if (op == InfixExpression.Operator.EQUALS) return "==";
        if (op == InfixExpression.Operator.NOT_EQUALS) return "!=";
        if (op == InfixExpression.Operator.LESS) return "<";
        if (op == InfixExpression.Operator.LESS_EQUALS) return "<=";
        if (op == InfixExpression.Operator.GREATER) return ">";
        if (op == InfixExpression.Operator.GREATER_EQUALS) return ">=";
        if (op == InfixExpression.Operator.CONDITIONAL_AND) return "&&";
        if (op == InfixExpression.Operator.CONDITIONAL_OR) return "||";
        return op.toString();
    }

    private static String prefixOp(PrefixExpression.Operator op) {
        if (op == PrefixExpression.Operator.NOT) return "!";
        if (op == PrefixExpression.Operator.COMPLEMENT) return "~";
        if (op == PrefixExpression.Operator.MINUS) return "-";
        if (op == PrefixExpression.Operator.PLUS) return "+";
        return op.toString();
    }

    private static String compoundAssignOp(Assignment.Operator op) {
        if (op == Assignment.Operator.PLUS_ASSIGN) return "+";
        if (op == Assignment.Operator.MINUS_ASSIGN) return "-";
        if (op == Assignment.Operator.TIMES_ASSIGN) return "*";
        if (op == Assignment.Operator.DIVIDE_ASSIGN) return "/";
        if (op == Assignment.Operator.BIT_AND_ASSIGN) return "&";
        if (op == Assignment.Operator.BIT_OR_ASSIGN) return "|";
        if (op == Assignment.Operator.BIT_XOR_ASSIGN) return "^";
        if (op == Assignment.Operator.REMAINDER_ASSIGN) return "%";
        if (op == Assignment.Operator.LEFT_SHIFT_ASSIGN) return "<<";
        if (op == Assignment.Operator.RIGHT_SHIFT_SIGNED_ASSIGN) return ">>";
        if (op == Assignment.Operator.RIGHT_SHIFT_UNSIGNED_ASSIGN) return ">>>";
        return op.toString();
    }
}
