using System.Runtime.InteropServices;

internal static class Native
{
    private const string Library = "cxpr";
    [DllImport(Library)] internal static extern IntPtr cxpr_expr_parser_new();
    [DllImport(Library)] internal static extern void cxpr_expr_parser_free(IntPtr parser);
    [DllImport(Library, CharSet = CharSet.Ansi)]
    internal static extern IntPtr cxpr_expr_ast_parse(IntPtr parser, string expression, IntPtr error);
    [DllImport(Library)] internal static extern void cxpr_expr_ast_free(IntPtr ast);
    [DllImport(Library)] internal static extern IntPtr cxpr_expr_compile(IntPtr ast, IntPtr registry, IntPtr error);
    [DllImport(Library)] internal static extern void cxpr_expr_compiled_free(IntPtr program);
    [DllImport(Library)] internal static extern IntPtr cxpr_context_new();
    [DllImport(Library)] internal static extern void cxpr_context_free(IntPtr context);
    [DllImport(Library, CharSet = CharSet.Ansi)]
    internal static extern void cxpr_context_set(IntPtr context, string name, double value);
    [DllImport(Library, CharSet = CharSet.Ansi)]
    internal static extern void cxpr_context_set_int64(IntPtr context, string name, long value);
    [DllImport(Library, CharSet = CharSet.Ansi)]
    internal static extern long cxpr_context_get_int64(
        IntPtr context, string name, [MarshalAs(UnmanagedType.I1)] out bool found);
    [DllImport(Library, CharSet = CharSet.Ansi)]
    internal static extern void cxpr_context_set_param_int64(
        IntPtr context, string name, long value);
    [DllImport(Library, CharSet = CharSet.Ansi)]
    internal static extern long cxpr_context_get_param_int64(
        IntPtr context, string name, [MarshalAs(UnmanagedType.I1)] out bool found);
    [DllImport(Library)]
    [return: MarshalAs(UnmanagedType.I1)]
    internal static extern bool cxpr_expr_compiled_eval_number(
        IntPtr program, IntPtr context, IntPtr registry, out double value, IntPtr error);
}

internal static class Program
{
    private static void Main()
    {
        var parser = Native.cxpr_expr_parser_new();
        var ast = Native.cxpr_expr_ast_parse(parser, "price * 2 + 1", IntPtr.Zero);
        var program = Native.cxpr_expr_compile(ast, IntPtr.Zero, IntPtr.Zero);
        var context = Native.cxpr_context_new();
        if (parser == IntPtr.Zero || ast == IntPtr.Zero || program == IntPtr.Zero || context == IntPtr.Zero)
            throw new InvalidOperationException("cxpr allocation or compilation failed");
        Native.cxpr_context_set(context, "price", 20.5);
        if (!Native.cxpr_expr_compiled_eval_number(
                program, context, IntPtr.Zero, out var value, IntPtr.Zero) || value != 42.0)
            throw new InvalidOperationException($"Expected 42, got {value}");
        const long exact = 9007199254740993L;
        Native.cxpr_context_set_int64(context, "order_id", exact);
        if (Native.cxpr_context_get_int64(context, "order_id", out var found) != exact || !found)
            throw new InvalidOperationException("int64 context P/Invoke lost precision");
        Native.cxpr_context_set_param_int64(context, "step", exact + 2);
        if (Native.cxpr_context_get_param_int64(context, "step", out found) != exact + 2 || !found)
            throw new InvalidOperationException("int64 parameter P/Invoke lost precision");
        Native.cxpr_context_free(context);
        Native.cxpr_expr_compiled_free(program);
        Native.cxpr_expr_ast_free(ast);
        Native.cxpr_expr_parser_free(parser);
        Console.WriteLine(".NET P/Invoke smoke: number and exact int64 bindings passed");
    }
}
