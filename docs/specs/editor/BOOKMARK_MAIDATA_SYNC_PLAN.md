---
lifecycle: archive-legacy
---

> 历史资料：保留当时的设计与实施背景，不代表当前产品路径。当前入口见 [架构与契约](../ui/CURRENT_ARCHITECTURE_ZH.md) 和 [文档索引](../../INDEX.md)。

# Bookmark maidata sync plan

Status: superseded by [BOOKMARK_REDESIGN_SPEC.md](BOOKMARK_REDESIGN_SPEC.md).

The earlier local-first proposal kept bookmark data in
`.miacode/miacode_settings.json` and treated `maidata.txt` sync as an optional
future export path. The current design changes that direction: bookmarks should
be stored in the simai file itself as one managed metadata field.

Current storage direction:

```txt
&miacode_bookmarks={"schema":"miacode_bookmarks_v2","items":[{"d":5,"l":8,"n":"Intro","s":8.796}]}
```

High-level rules retained from the old plan:

- Store all bookmark payload in one single-line field.
- Store the difficulty id with each bookmark.
- Bad bookmark metadata must not block chart loading.
- If compact JSON proves fragile with external editors, switch the value body to
  a `miacode:v2:<base64url-json>` payload.

See the redesign spec for the sidebar, naming, migration, and hidden-field
filtering details.
