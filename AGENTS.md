# Commit identity for mnOS

This repository belongs to `zengyiqian2014-boop`. Before creating a commit on
the owner's behalf, set and verify the identity in this repository only:

```sh
git config --local user.name zengyiqian2014-boop
git config --local user.email 235238542+zengyiqian2014-boop@users.noreply.github.com
git var GIT_AUTHOR_IDENT
git var GIT_COMMITTER_IDENT
```

Authentication to GitHub does not select the Git author or committer. Do not
reuse an unrelated identity inherited from the execution environment. Check for
`GIT_AUTHOR_*` and `GIT_COMMITTER_*` overrides without dumping the environment.
Preserve original third-party attribution and license notices. Do not rewrite
published history unless the user has requested that correction.
