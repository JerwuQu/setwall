# setwall

Small CLI to set wallpapers for multiple displays in Windows.

# Usage

Output from `setwall.exe -h`:

```
USAGE:
	Setting wallpapers:
		setwall.exe [OPTIONS] <wallpaper path(s)...>
	Getting info:
		setwall.exe <OPTIONS...>
OPTIONS:
	-h               Get help and exit
	-i               Get display info and exit
	-o <file.png>    Specify wallpaper location (defaults to a temporary file)
	-d               Only generate output, don't set it as wallpaper
```

# Build

See [the Github Actions build defs](https://github.com/JerwuQu/setwall/blob/master/.github/workflows/build.yml).

# License

[MIT](https://github.com/JerwuQu/setwall/blob/master/LICENSE)
