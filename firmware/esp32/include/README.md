Firmware credentials are generated from the repository root .env by pnpm firmware:build. The generated secrets.h file is gitignored and must never be committed.

For manual builds, copy secrets.example.h to secrets.h and populate values locally.
