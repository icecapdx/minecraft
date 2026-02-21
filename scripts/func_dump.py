import ida_funcs
import idautils

OUTPUT_PATH = "../minecraft_config.toml"

def main():
    try:
        with open(OUTPUT_PATH, "a", encoding="utf-8") as f:
            f.write("\n[functions]\n")

            for start in idautils.Functions():
                name = ida_funcs.get_func_name(start)
                if not name.startswith("sub_"):
                    continue

                func = ida_funcs.get_func(start)
                if not func:
                    continue

                size = func.end_ea - func.start_ea
                f.write(
                    f'0x{func.start_ea:08X} = {{ name = "{name}", size = 0x{size:X} }}\n'
                )

        print("Done.")

    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    main()
