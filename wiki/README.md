# Publishing the GitHub Wiki

`wiki/docs` is the source of truth. GitHub's Wiki lives in a separate Git
repository; a main-repository release or GitHub Pages deployment does not
update it.

After committing the Markdown updates, use a local clone of
`https://github.com/1vivy/gbl_root_canoe.wiki.git`:

```sh
python3 scripts/sync_github_wiki.py /path/to/wiki-checkout
python3 scripts/sync_github_wiki.py /path/to/wiki-checkout --check
```

The script mirrors the English and Chinese pages, converts local Markdown links
to Wiki URLs, generates the home pages and sidebar, and retains old installation
scenario URLs as short pointers. Code fences remain unchanged. Unmapped source
or Wiki pages stop the export for review. It does not commit, push, or delete
pages. Review the Wiki diff before committing and pushing that repository.

The VitePress `index.md` files contain home-page metadata rather than article
content. The exporter generates normal Markdown home pages for GitHub instead.
