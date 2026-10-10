# Historical archives

`migrations/*.tar.gz` preserves the original legacy data layout, including comments
and retired content. Archive file bytes were verified against the original files
before removing the unpacked tracked copies. These archives are not game definitions
or active story content. Inspect or extract them with any tar/gzip utility.

Future automatic data migrations still retain their uncompressed originals in
`backups/data-layout-<number>/`. That runtime directory is ignored by Git.
