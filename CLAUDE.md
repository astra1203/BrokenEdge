# Claude Code notes

## Reading files
- Use the Read, Glob and Grep tools to read and search files. Do not use `cat`, `type`, `Get-Content`, or shell `for` loops over files to read them.
- Avoid shell variables (`$f`, `$USERPROFILE`, `%USERPROFILE%`) and loops in Bash/PowerShell commands when a dedicated tool or a literal path works: commands with variables can't be analyzed and always trigger a permission prompt.
- The user-level Claude config lives at `C:/Users/blq01/.claude`; refer to it by that literal path.
