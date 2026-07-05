# Bookmark maidata sync plan

MiaCode bookmarks are local-first. The primary store remains
`.miacode/miacode_settings.json` beside the chart file, so normal chart saves do
not add private editing notes to `maidata.txt`.

If bookmark sharing through `maidata.txt` is added later, use one single-line
metadata field:

```txt
&bookmark={"version":1,"items":[{"difficulty_id":4,"title":"Chorus","text":"Check 1/5 overlap","line":42,"second":36.5}]}
```

Rules for a future implementation:

- Never write real newlines into `&bookmark=`.
- Store the difficulty id with each bookmark. Bookmarks are scoped to one
  difficulty and must not appear on other difficulty charts.
- Reject or normalize bookmark title/note input that contains line breaks before
  syncing to `maidata.txt`.
- Write compact JSON only. If compatibility testing shows another editor
  damages JSON punctuation or non-ASCII text, switch to
  `&bookmark=miacode:v1:<base64url-json>`.
- Keep `.miacode/miacode_settings.json` as the source of truth unless the user
  explicitly imports from `maidata.txt`.
- Do not sync to `maidata.txt` by default. Provide explicit "sync/export to
  maidata" and "remove bookmark metadata from maidata" actions.
- Bad or unknown bookmark metadata must not block chart loading; report it as a
  bookmark import problem only.
