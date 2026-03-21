# Fence Ranger Firmware

Install Platformio, the easiest method is probably using VSCode: https://platformio.org/platformio-ide

Make sure to get the git submodules, because lib/RadioBoards is a submodule:

```
git submodule update --init --recursive
```


## Without VSCode on MacOS with fish shell

Install using the script from: https://docs.platformio.org/en/latest/core/installation/methods/installer-script.html

To activate the new local environment with fish:

```
source ~/.platformio/penv/bin/activate.fish
```

Compile the firmware:

```
pio run -e heltec_wifi_lora_32_V4
```

To generate compile_commands.json for other IDE support:

```
pio run -e heltec_wifi_lora_32_V4 -t compiledb
```

## Make the language server work with Neovim

The only way I got this to work properly was by downloading the espressif clangd fork. Download the latest from here: https://github.com/espressif/llvm-project/releases. I used esp-21.1.3_20260304.

In your preferred directory:
```
git clone --depth 1 --branch esp-21.1.3_20260304 https://github.com/espressif/llvm-project.git
```

Compile it, takes a while:

```
cd llvm-project
cmake -S llvm -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra"
cmake --build build
```

Configure clangd by creating a `.clangd` file like this in this projects root directory:

```
require("lspconfig").clangd.setup({
	cmd = {
		"/Users/bart/git/espressif/llvm-project/build/bin/clangd",
		"--background-index",
		"--query-driver=/Users/bart/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-g++",
	},
})
```

And create a `.nvim.lua` file, also in this projects root directory:

```
-- .nvim.lua
vim.lsp.config("clangd", {
	cmd = {
		"/Users/bart/git/espressif/llvm-project/build/bin/clangd",
		"--background-index",
		"--query-driver=/Users/bart/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-g++",
	},
	root_markers = { "compile_commands.json" },
})
vim.lsp.enable("clangd")

vim.api.nvim_create_autocmd("BufWritePre", {
  pattern = { "*.cpp", "*.h", "*.hpp", "*.c" },
  callback = function()
    vim.lsp.buf.format()
  end,
})
```

Besides the language server configuration, it also configures auto-format on save
