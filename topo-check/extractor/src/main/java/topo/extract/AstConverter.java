// AstConverter: Converts Eclipse JDT AST nodes to TranspileModel JSON objects.
//
// Each convert* method returns a JsonObject with a "kind" field matching the
// TranspileModel node types defined in topo-core/include/topo/Transpile/TranspileModel.h.

package topo.extract;

import com.google.gson.JsonArray;
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
            return unsupportedStmt("throw statement");
        } else if (stmt instanceof TryStatement) {
            return unsupportedStmt("try statement");
        } else if (stmt instanceof SwitchStatement) {
            return unsupportedStmt("switch statement");
        } else if (stmt instanceof DoStatement ds) {
            return convertDoWhileStmt(ds);
        } else if (stmt instanceof BreakStatement) {
            JsonObject obj = new JsonObject();
            obj.addProperty("kind", "break");
            return obj;
        } else if (stmt instanceof ContinueStatement) {
            JsonObject obj = new JsonObject();
            obj.addProperty("kind", "continue");
            return obj;
        } else {
            return unsupportedStmt("unhandled statement: " + stmt.getClass().getSimpleName());
        }
    }

    private static JsonObject convertVarDeclStmt(VariableDeclarationStatement vds) {
        @SuppressWarnings("unchecked")
        List<VariableDeclarationFragment> frags = vds.fragments();
        if (frags.isEmpty()) {
            return unsupportedStmt("empty variable declaration");
        }
        // The model carries one variable per VarDeclStmt. A multi-fragment
        // declaration (`int x = 1, y = 2;`) cannot be represented losslessly,
        // so decline rather than silently drop the trailing fragments.
        if (frags.size() > 1) {
            return unsupportedStmt("multi-fragment variable declaration");
        }

        VariableDeclarationFragment frag = frags.get(0);
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "vardecl");
        obj.addProperty("name", frag.getName().getIdentifier());
        obj.add("type", convertType(vds.getType()));
        // omit-when-absent: the C++ VarDecl deserializer reads `init` only when
        // the key is present (a null value would throw in deserializeExpr).
        if (frag.getInitializer() != null) {
            obj.add("init", convertExpression(frag.getInitializer()));
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
        obj.addProperty("kind", "exprstmt");
        obj.add("expr", convertExpression(expr));
        return obj;
    }

    private static JsonObject convertReturnStmt(ReturnStatement rs) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "return");
        // omit-when-absent: a void `return;` carries no `value` key (a null
        // value would throw in the C++ Return deserializer).
        if (rs.getExpression() != null) {
            obj.add("value", convertExpression(rs.getExpression()));
        }
        return obj;
    }

    private static JsonObject convertIfStmt(IfStatement is) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "if");
        obj.add("condition", convertExpression(is.getExpression()));
        obj.add("thenBody", statementsToArray(is.getThenStatement()));
        // omit-when-absent: the C++ If deserializer reads `elseBody` only when
        // the key is present.
        if (is.getElseStatement() != null) {
            obj.add("elseBody", statementsToArray(is.getElseStatement()));
        }
        return obj;
    }

    private static JsonObject convertForStmt(ForStatement fs) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "for");

        // The model carries a single init statement / single increment
        // expression. A C-style multi-init / multi-updater for-loop
        // (`for (i=0, j=0; ...; i++, j++)`) cannot be represented losslessly,
        // so decline the whole loop rather than silently dropping the tail.
        @SuppressWarnings("unchecked")
        List<Expression> inits = fs.initializers();
        @SuppressWarnings("unchecked")
        List<Expression> updaters = fs.updaters();
        if (inits.size() > 1 || updaters.size() > 1) {
            return unsupportedStmt("multi-clause for-loop");
        }

        // The C++ `for.init` is a STATEMENT (omit-when-absent). Wrap the Java
        // init expression in an exprstmt so it deserializes correctly; a raw
        // expression node in init position would be misread as a statement
        // kind and throw.
        if (!inits.isEmpty()) {
            JsonObject initStmt = new JsonObject();
            initStmt.addProperty("kind", "exprstmt");
            initStmt.add("expr", convertExpression(inits.get(0)));
            obj.add("init", initStmt);
        }

        if (fs.getExpression() != null) {
            obj.add("condition", convertExpression(fs.getExpression()));
        }

        if (!updaters.isEmpty()) {
            obj.add("increment", convertExpression(updaters.get(0)));
        }

        obj.add("body", statementsToArray(fs.getBody()));
        return obj;
    }

    private static JsonObject convertEnhancedForStmt(EnhancedForStatement efs) {
        // The model's ForStmt has no foreach (variable/iterable) shape; mapping
        // it onto a C-style `for` would silently drop the iteration. Decline.
        return unsupportedStmt("enhanced-for statement");
    }

    private static JsonObject convertWhileStmt(WhileStatement ws) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "while");
        obj.add("condition", convertExpression(ws.getExpression()));
        obj.add("body", statementsToArray(ws.getBody()));
        return obj;
    }

    private static JsonObject convertDoWhileStmt(DoStatement ds) {
        // The model's WhileStmt is run-zero-or-more; a do-while is
        // run-once-minimum. Mapping it onto `while` would silently flip the
        // execution-at-least-once semantics, so decline.
        return unsupportedStmt("do-while statement");
    }

    private static JsonObject convertBlockStmt(Block block) {
        // The model has no bare-block statement kind. Decline rather than
        // emit an unrecognized kind that would fall through to exprstmt and
        // throw in the C++ deserializer.
        return unsupportedStmt("block statement");
    }

    private static JsonObject convertAssignment(Assignment assign) {
        JsonObject obj = new JsonObject();
        if (assign.getOperator() == Assignment.Operator.ASSIGN) {
            obj.addProperty("kind", "assign");
            obj.add("target", convertExpression(assign.getLeftHandSide()));
            obj.add("value", convertExpression(assign.getRightHandSide()));
        } else {
            // Compound assignments (+=, -=, etc.) -> `target = target <op> rhs`,
            // i.e. an assign whose value is a binaryop. The op MUST be the
            // word-form the C++ BinaryOp deserializer accepts.
            obj.addProperty("kind", "assign");
            obj.add("target", convertExpression(assign.getLeftHandSide()));

            JsonObject binOp = new JsonObject();
            binOp.addProperty("kind", "binaryop");
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
            obj.addProperty("kind", "varref");
            obj.addProperty("name", "this");
            return obj;
        } else if (expr instanceof ParenthesizedExpression pe) {
            return convertExpression(pe.getExpression());
        } else if (expr instanceof CastExpression ce) {
            // The model has no cast expression node. Decline rather than emit
            // an unrecognized kind that would throw in the C++ deserializer.
            return unsupportedExpr("cast expression");
        } else if (expr instanceof ConditionalExpression ce) {
            return convertConditional(ce);
        } else if (expr instanceof ArrayCreation) {
            return unsupportedExpr("array creation expression");
        } else if (expr instanceof ArrayInitializer) {
            return unsupportedExpr("array initializer expression");
        } else if (expr instanceof InstanceofExpression) {
            return unsupportedExpr("instanceof expression");
        } else if (expr instanceof LambdaExpression) {
            return unsupportedExpr("lambda expression");
        } else if (expr instanceof MethodReference) {
            return unsupportedExpr("method reference expression");
        } else if (expr instanceof SuperMethodInvocation) {
            return unsupportedExpr("super method invocation");
        } else if (expr instanceof Assignment) {
            // An assignment in expression position (e.g. `while ((x = f()))`)
            // has no model representation; the statement-level path handles the
            // common `x = ...;` case before reaching here.
            return unsupportedExpr("assignment expression");
        } else {
            return unsupportedExpr("unhandled expression: " + expr.getClass().getSimpleName());
        }
    }

    private static JsonObject convertInfix(InfixExpression ie) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "binaryop");
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
                wrapper.addProperty("kind", "binaryop");
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
        PrefixExpression.Operator op = pe.getOperator();
        // Unary plus is the identity on its operand; the model has no unary-plus
        // op, so pass the operand through unchanged rather than corrupting it
        // into a negate.
        if (op == PrefixExpression.Operator.PLUS) {
            return convertExpression(pe.getOperand());
        }
        // Increment/decrement and the remaining unary operators all map onto
        // the model's UnaryOp vocabulary (word-form ops), which is a valid
        // expression node in every position. The C++ UnaryOp deserializer
        // accepts preincrement / predecrement directly.
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "unaryop");
        if (op == PrefixExpression.Operator.INCREMENT) {
            obj.addProperty("op", "preincrement");
        } else if (op == PrefixExpression.Operator.DECREMENT) {
            obj.addProperty("op", "predecrement");
        } else {
            obj.addProperty("op", prefixOp(op));
        }
        obj.add("operand", convertExpression(pe.getOperand()));
        return obj;
    }

    private static JsonObject convertPostfix(PostfixExpression pfe) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "unaryop");
        obj.addProperty("op",
                pfe.getOperator() == PostfixExpression.Operator.INCREMENT ? "postincrement" : "postdecrement");
        obj.add("operand", convertExpression(pfe.getOperand()));
        return obj;
    }

    private static JsonObject convertMethodInvocation(MethodInvocation mi) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "call");

        // The model's CallExpr.callee is a flat qualified-name STRING, not an
        // expression node. Build a dotted callee from the receiver (when it is
        // a plain name chain) plus the method name; an unqualified call is just
        // the method name.
        String member = mi.getName().getIdentifier();
        String callee;
        if (mi.getExpression() != null) {
            String receiver = flattenName(mi.getExpression());
            callee = (receiver != null) ? receiver + "." + member : member;
        } else {
            callee = member;
        }
        obj.addProperty("callee", callee);

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
        obj.addProperty("kind", "memberaccess");
        obj.add("object", convertExpression(fa.getExpression()));
        obj.addProperty("member", fa.getName().getIdentifier());
        return obj;
    }

    private static JsonObject convertQualifiedName(QualifiedName qn) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "memberaccess");
        obj.add("object", convertExpression(qn.getQualifier()));
        obj.addProperty("member", qn.getName().getIdentifier());
        return obj;
    }

    private static JsonObject convertArrayAccess(ArrayAccess aa) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "index");
        obj.add("object", convertExpression(aa.getArray()));
        obj.add("index", convertExpression(aa.getIndex()));
        return obj;
    }

    private static JsonObject convertNumberLiteral(NumberLiteral nl) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "literal");
        String token = nl.getToken();
        // Detect a hex/binary integer prefix before the decimal float test:
        // a hex literal can legitimately end in d/D/f/F (e.g. 0xFF, 0x1d) and
        // must stay an integer.
        String lower = token.toLowerCase();
        boolean hexOrBin = lower.startsWith("0x") || lower.startsWith("0b");
        boolean isFloat;
        if (hexOrBin) {
            // Hex floats (0x1p3) carry a 'p' exponent; a plain hex/binary
            // integer never does.
            isFloat = lower.indexOf('p') >= 0;
        } else {
            isFloat = token.contains(".") || lower.endsWith("f") || lower.endsWith("d")
                    || lower.indexOf('e') >= 0;
        }
        obj.addProperty("litKind", isFloat ? "float" : "integer");
        obj.addProperty("value", token);
        return obj;
    }

    private static JsonObject convertStringLiteral(StringLiteral sl) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "literal");
        obj.addProperty("litKind", "string");
        obj.addProperty("value", sl.getLiteralValue());
        return obj;
    }

    private static JsonObject convertBooleanLiteral(BooleanLiteral bl) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "literal");
        obj.addProperty("litKind", "boolean");
        obj.addProperty("value", String.valueOf(bl.booleanValue()));
        return obj;
    }

    private static JsonObject convertCharLiteral(CharacterLiteral cl) {
        // The model has no char literal kind; the closest faithful mapping is
        // a string-kind literal carrying the single character. (Mapping it to
        // an integer would silently change semantics.)
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "literal");
        obj.addProperty("litKind", "string");
        obj.addProperty("value", String.valueOf(cl.charValue()));
        return obj;
    }

    private static JsonObject convertNullLiteral() {
        // The model has no null literal kind. Emit a string-kind literal whose
        // value is "null"; declining would otherwise lose a legitimate operand.
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "literal");
        obj.addProperty("litKind", "string");
        obj.addProperty("value", "null");
        return obj;
    }

    private static JsonObject convertSimpleName(SimpleName sn) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "varref");
        obj.addProperty("name", sn.getIdentifier());
        return obj;
    }

    private static JsonObject convertClassInstanceCreation(ClassInstanceCreation cic) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "construct");
        // The model's ConstructExpr carries a TypeNode (`type`), not a string
        // type name. Lift the JDT type so it deserializes as a TypeNode.
        obj.add("type", convertType(cic.getType()));

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
        // Ternary: cond ? then : else -> the model's dedicated ternary
        // expression node (an IfStmt would be a statement kind in expression
        // position and throw in the C++ deserializer).
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "ternary");
        obj.add("condition", convertExpression(ce.getExpression()));
        obj.add("trueExpr", convertExpression(ce.getThenExpression()));
        obj.add("falseExpr", convertExpression(ce.getElseExpression()));
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

    // Flatten a simple name / qualified name / `this` chain into a dotted
    // string for use as a CallExpr callee. Returns null when the receiver is
    // not a plain name chain (e.g. a method call or array access), in which
    // case the caller falls back to the bare method name.
    private static String flattenName(Expression expr) {
        if (expr instanceof SimpleName sn) {
            return sn.getIdentifier();
        }
        if (expr instanceof QualifiedName qn) {
            String q = flattenName(qn.getQualifier());
            return (q != null) ? q + "." + qn.getName().getIdentifier() : null;
        }
        if (expr instanceof ThisExpression) {
            return "this";
        }
        if (expr instanceof FieldAccess fa) {
            String o = flattenName(fa.getExpression());
            return (o != null) ? o + "." + fa.getName().getIdentifier() : null;
        }
        return null;
    }

    // Unsupported node in EXPRESSION position: a `unsupported` expr carrying a
    // `description`, which the C++ deserializeExpr accepts directly.
    private static JsonObject unsupportedExpr(String description) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "unsupported");
        obj.addProperty("description", description);
        return obj;
    }

    // Unsupported node in STATEMENT position: wrap the `unsupported` expr in an
    // `exprstmt` so it deserializes as an ExprStmt (an `unsupported` kind in
    // statement position would fall through to ExprStmt and then throw on the
    // missing `expr` key).
    private static JsonObject unsupportedStmt(String description) {
        JsonObject obj = new JsonObject();
        obj.addProperty("kind", "exprstmt");
        obj.add("expr", unsupportedExpr(description));
        return obj;
    }

    // The C++ BinaryOp deserializer accepts only word-form op names and maps
    // every unrecognized string to Shr; symbolic ops would silently corrupt.
    // JDT's AND/OR are the BITWISE operators (`&`/`|`); only CONDITIONAL_AND/
    // CONDITIONAL_OR are the logical (`&&`/`||`) ones.
    private static String infixOp(InfixExpression.Operator op) {
        if (op == InfixExpression.Operator.PLUS) return "add";
        if (op == InfixExpression.Operator.MINUS) return "sub";
        if (op == InfixExpression.Operator.TIMES) return "mul";
        if (op == InfixExpression.Operator.DIVIDE) return "div";
        if (op == InfixExpression.Operator.REMAINDER) return "mod";
        if (op == InfixExpression.Operator.AND) return "bitand";
        if (op == InfixExpression.Operator.OR) return "bitor";
        if (op == InfixExpression.Operator.XOR) return "bitxor";
        if (op == InfixExpression.Operator.LEFT_SHIFT) return "shl";
        if (op == InfixExpression.Operator.RIGHT_SHIFT_SIGNED) return "shr";
        // No model op for the unsigned right shift (`>>>`); shr is the closest.
        if (op == InfixExpression.Operator.RIGHT_SHIFT_UNSIGNED) return "shr";
        if (op == InfixExpression.Operator.EQUALS) return "eq";
        if (op == InfixExpression.Operator.NOT_EQUALS) return "noteq";
        if (op == InfixExpression.Operator.LESS) return "less";
        if (op == InfixExpression.Operator.LESS_EQUALS) return "lesseq";
        if (op == InfixExpression.Operator.GREATER) return "greater";
        if (op == InfixExpression.Operator.GREATER_EQUALS) return "greatereq";
        if (op == InfixExpression.Operator.CONDITIONAL_AND) return "and";
        if (op == InfixExpression.Operator.CONDITIONAL_OR) return "or";
        return op.toString();
    }

    // The C++ UnaryOp deserializer accepts word-form names and maps unknowns to
    // negate. Unary plus is handled (passed through) by convertPrefix before it
    // reaches here.
    private static String prefixOp(PrefixExpression.Operator op) {
        if (op == PrefixExpression.Operator.NOT) return "not";
        if (op == PrefixExpression.Operator.COMPLEMENT) return "bitnot";
        if (op == PrefixExpression.Operator.MINUS) return "negate";
        return op.toString();
    }

    // Word-form op for the binary side of a compound assignment (`a += b` ⇒
    // `a = a add b`). Mirrors infixOp's vocabulary; AND/OR here are the bitwise
    // compound forms (`&=`/`|=`).
    private static String compoundAssignOp(Assignment.Operator op) {
        if (op == Assignment.Operator.PLUS_ASSIGN) return "add";
        if (op == Assignment.Operator.MINUS_ASSIGN) return "sub";
        if (op == Assignment.Operator.TIMES_ASSIGN) return "mul";
        if (op == Assignment.Operator.DIVIDE_ASSIGN) return "div";
        if (op == Assignment.Operator.BIT_AND_ASSIGN) return "bitand";
        if (op == Assignment.Operator.BIT_OR_ASSIGN) return "bitor";
        if (op == Assignment.Operator.BIT_XOR_ASSIGN) return "bitxor";
        if (op == Assignment.Operator.REMAINDER_ASSIGN) return "mod";
        if (op == Assignment.Operator.LEFT_SHIFT_ASSIGN) return "shl";
        if (op == Assignment.Operator.RIGHT_SHIFT_SIGNED_ASSIGN) return "shr";
        if (op == Assignment.Operator.RIGHT_SHIFT_UNSIGNED_ASSIGN) return "shr";
        return op.toString();
    }
}
