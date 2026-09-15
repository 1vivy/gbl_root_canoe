#!/usr/bin/env python3
"""Mirror checked-in Markdown into a separately cloned GitHub Wiki.

Only links and the VitePress home/navigation presentation are converted.
The destination must be a Git checkout. This script never commits or pushes.
"""

import argparse
from pathlib import Path
import re
import subprocess
from urllib.parse import quote, urlsplit


ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / "wiki/docs"
REPO = "https://github.com/1vivy/gbl_root_canoe"
WIKI = REPO + "/wiki/"
EN = {
    "intro": "Intro", "install": "Install", "linux-usb": "Linux-USB",
    "reinstall": "Reinstall", "usage": "Usage", "ota": "OTA",
    "uninstall": "Uninstall", "mass-storage": "Mass-Storage",
    "canoe-cfg": "Canoe-Cfg", "commands": "Commands",
    "format-data": "Format-Data", "chainload": "Chainload",
    "build": "Build", "release": "Release", "changelog": "Changelog",
    "contribute": "Contribute", "contributors": "Contributors",
}
ZH = {
    "intro": "总览-Intro", "install": "Install-安装",
    "linux-usb": "Linux-USB-访问", "usage": "使用说明-Usage",
    "ota": "OTA-更新", "uninstall": "卸载-Uninstall",
    "mass-storage": "USB-大容量存储", "canoe-cfg": "BDS-配置",
    "chainload": "链式启动-Chainload", "build": "构建-Build",
    "release": "发布-Release", "changelog": "更新日志-Changelog",
    "contribute": "贡献-Contribute", "contributors": "贡献者-Contributors",
}
PAGES = {DOCS / (key + ".md"): value for key, value in EN.items()}
PAGES.update({DOCS / "zh" / (key + ".md"): value for key, value in ZH.items()})
LINK = re.compile(r"(!?\[[^\]]*\]\()([^\s)]+)(\))")
FENCE = re.compile(r"^(`{3,}|~{3,})", re.MULTILINE)


def wiki_url(slug):
    return WIKI + quote(slug)


def convert_links(text, source):
    """Do not rewrite command examples, even if they contain Markdown syntax."""
    fence = None
    result = []
    for line in text.splitlines(keepends=True):
        marker = FENCE.match(line)
        if marker:
            token = marker.group(1)
            if fence is None:
                fence = token
            elif token[0] == fence[0] and len(token) >= len(fence):
                fence = None
            result.append(line)
            continue
        if fence:
            result.append(line)
            continue

        def replace(match):
            href = match.group(2)
            parts = urlsplit(href)
            if parts.scheme or parts.netloc or not parts.path:
                return match.group(0)
            base = DOCS if parts.path.startswith("/") else source.parent
            path = (base / parts.path.lstrip("/")).resolve()
            if path in PAGES:
                target = wiki_url(PAGES[path])
            elif path == DOCS / "index.md":
                target = wiki_url("Home")
            elif path == DOCS / "zh/index.md":
                target = wiki_url("中文首页")
            elif path.is_file() and path.is_relative_to(ROOT):
                target = REPO + "/blob/main/" + quote(path.relative_to(ROOT).as_posix())
            else:
                raise ValueError(f"Unresolved link in {source}: {href}")
            if parts.query:
                target += "?" + parts.query
            if parts.fragment:
                target += "#" + parts.fragment
            return match.group(1) + target + match.group(3)

        result.append(LINK.sub(replace, line))
    return "".join(result)


def navigation(mapping, prefix=""):
    rows = []
    for key, slug in mapping.items():
        source = DOCS / prefix / (key + ".md")
        title = next(line[2:] for line in source.read_text().splitlines() if line.startswith("# "))
        rows.append(f"- [{title}]({wiki_url(slug)})")
    return "\n".join(rows)


def render():
    sources = set(DOCS.rglob("*.md")) - {DOCS / "index.md", DOCS / "zh/index.md"}
    if sources != set(PAGES):
        raise ValueError(f"Update the page map: {sources.symmetric_difference(PAGES)}")
    rendered = {}
    for source, slug in PAGES.items():
        rendered[slug + ".md"] = convert_links(source.read_text(), source)
    en_nav, zh_nav = navigation(EN), navigation(ZH, "zh")
    rendered["Home.md"] = (
        "# GBL Root Canoe fork — documentation\n\n"
        "This wiki mirrors the current Markdown in `wiki/docs` in the fork repository.\n"
        "The installation and reference pages describe Canoe 7.x. References to 6.3.5\n"
        "in the changelog, reinstallation notes, and contributor history are historical.\n\n"
        f"[Releases]({REPO}/releases) · [Canoe Boot Manager](https://canoe-boot-manager.1vv.ca/)"
        f" · [Source]({REPO}) · [中文首页]({wiki_url('中文首页')})\n\n"
        "## English\n\n" + en_nav + "\n\n## 简体中文\n\n" + zh_nav + "\n"
    )
    rendered["中文首页.md"] = (
        "# GBL Root Canoe fork — 中文文档\n\n"
        "本 Wiki 与本分支仓库的 `wiki/docs` Markdown 同步。安装和参考页面对应 Canoe 7.x；"
        "更新日志、旧版重装和贡献历史中的 6.3.5 信息仅用于历史说明。\n\n"
        + zh_nav + f"\n\n[英文文档]({wiki_url('Home')})\n"
    )
    rendered["_Sidebar.md"] = (
        f"[Home]({wiki_url('Home')}) · [中文首页]({wiki_url('中文首页')})\n\n"
        "### English\n\n" + en_nav + "\n\n### 简体中文\n\n" + zh_nav + "\n"
    )
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    rendered["_Footer.md"] = (
        f"Mirrored from [wiki/docs]({REPO}/tree/main/wiki/docs) · "
        f"[Source revision {revision[:12]}]({REPO}/commit/{revision}) · "
        "Edit the repository Markdown, then run `scripts/sync_github_wiki.py`.\n"
    )
    rendered["Install-Paths.md"] = (
        "# Installation paths\n\n"
        "The old scenario guide is retired.\n\n"
        f"See [Install]({wiki_url('Install')}) and [Reinstall]({wiki_url('Reinstall')}) "
        "for the current documentation.\n"
    )
    rendered["安装场景-Install-Paths.md"] = (
        "# 安装场景\n\n旧版分场景教程已退役。\n\n"
        f"请参见[安装]({wiki_url('Install-安装')})与[旧版重装]({wiki_url('Reinstall')})。\n"
    )
    return rendered


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkout", type=Path, help="Local clone of gbl_root_canoe.wiki.git")
    parser.add_argument("--check", action="store_true", help="Report drift without writing")
    args = parser.parse_args()
    checkout = args.checkout.resolve()
    if not (checkout / ".git").exists() or checkout == ROOT:
        parser.error("Destination must be a separate Git checkout")
    expected = render()
    extras = {p.name for p in checkout.glob("*.md")} - set(expected)
    if extras:
        parser.error(f"Unmapped wiki pages need review before publication: {sorted(extras)}")
    drift = []
    for name, content in expected.items():
        target = checkout / name
        if not target.exists() or target.read_text() != content:
            drift.append(name)
            if not args.check:
                target.write_text(content)
    print(f"{len(expected)} pages; {len(drift)} {'differ' if args.check else 'updated'}")
    if args.check and drift:
        print("\n".join(drift))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
