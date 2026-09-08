import { defineConfig } from 'vitepress'

export default defineConfig({
  base: '/gbl_root_canoe/',
  title: 'GBL Root Canoe',

  locales: {
    root: {
      label: 'English',
      lang: 'en-US',
      title: 'GBL Root Canoe',
      description: 'GBL Root Canoe — Fake Lock Bootloader',
      themeConfig: {
        socialLinks: [
          { icon: 'github', link: 'https://github.com/1vivy/gbl_root_canoe' },
        ],
        sidebar: {
          '/': [
            {
              items: [
                { text: 'Intro', link: '/intro' },
                { text: 'Install', link: '/install' },
                { text: 'Reinstall older mods', link: '/reinstall' },
                { text: 'Command-line tools', link: '/commands' },
                { text: 'Format-data matrix', link: '/format-data' },
                { text: 'OTA', link: '/ota' },
                { text: 'Usage', link: '/usage' },
                { text: 'Uninstall', link: '/uninstall' },
                { text: 'Build', link: '/build' },
                { text: 'Release', link: '/release' },
                { text: 'Contribute', link: '/contribute' },
                { text: 'Contributors', link: '/contributors' },
              ]
            },
          ]
        },
      },
    },
    zh: {
      label: '简体中文',
      lang: 'zh-CN',
      title: 'GBL Root Canoe',
      description: 'GBL Root Canoe — 假回锁 Bootloader 方案',
      themeConfig: {
        socialLinks: [
          { icon: 'github', link: 'https://github.com/1vivy/gbl_root_canoe' },
        ],
        sidebar: {
          '/zh/': [
            {
              items: [
                { text: '总览', link: '/zh/intro' },
                { text: '安装', link: '/zh/install' },
                { text: 'OTA 更新', link: '/zh/ota' },
                { text: '使用说明', link: '/zh/usage' },
                { text: '卸载', link: '/zh/uninstall' },
                { text: '构建', link: '/zh/build' },
                { text: '发布', link: '/zh/release' },
                { text: '贡献', link: '/zh/contribute' },
                { text: '贡献者', link: '/zh/contributors' },
              ]
            },
          ]
        },
      },
    },
  },
})
