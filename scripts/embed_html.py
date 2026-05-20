import os
Import("env")

src_dir = env.subst("$PROJECT_SRC_DIR")
html_path = os.path.join(src_dir, "ota.html")
header_path = os.path.join(src_dir, "ota_html.h")

with open(html_path, "r") as f:
    html = f.read()

with open(header_path, "w") as f:
    f.write("#pragma once\n\n")
    f.write('const char OTA_HTML[] = R"HTML(\n')
    f.write(html)
    f.write(')HTML";\n')
