"""
Export user-named functions from IDA as ReXGlue TOML.
"""

import os

import idaapi
import ida_funcs
import ida_kernwin
import ida_nalt
import idautils
import idc


def is_import(ea):
    """Check if a function is an imported symbol."""
    node = ida_nalt.get_import_module_qty()
    for i in range(node):
        def callback(ea_imp, name, ordinal):
            if ea_imp == ea:
                callback.found = True
                return False  # stop enumeration
            return True  # continue
        callback.found = False
        ida_nalt.enum_import_names(i, callback)
        if callback.found:
            return True
    return False


def collect_import_eas():
    """Build a set of all imported function addresses for lookup."""
    imports = set()
    for i in range(ida_nalt.get_import_module_qty()):
        def callback(ea, name, ordinal):
            if ea != idaapi.BADADDR:
                imports.add(ea)
            return True
        ida_nalt.enum_import_names(i, callback)
    return imports


def export_named_functions():
    import_eas = collect_import_eas()

    entries = []
    for ea in idautils.Functions():
        name = idc.get_func_name(ea)
        if not name:
            continue
            
        # Skip auto-generated names (sub_XXXX, nullsub_X, etc.)
        if name.startswith(("nullsub_", "__rest", "__save", "j_", "__", "start")):
            continue

        # Skip imports
        if ea in import_eas:
            continue

        func = ida_funcs.get_func(ea)
        if not func:
            continue
        size = func.end_ea - func.start_ea
        entries.append((ea, name, size))

    if not entries:
        ida_kernwin.info("No user-named functions found.")
        return

    # Build TOML content
    lines = ["[functions]"]
    for ea, name, size in entries:
        lines.append(f'0x{ea:08X} = {{ name = "rex_{name}", size = 0x{size:X} }}')

    toml_text = "\n".join(lines) + "\n"

    # Prompt user for save path
    default_path = os.path.join(os.path.dirname(idc.get_idb_path()), "named_functions.toml")
    out_path = ida_kernwin.ask_file(True, default_path, "Save named functions TOML")
    if not out_path:
        return

    with open(out_path, "w", encoding="utf-8") as f:
        f.write(toml_text)

    ida_kernwin.info(f"Exported {len(entries)} named functions to:\n{out_path}")


if __name__ == "__main__":
    export_named_functions()
