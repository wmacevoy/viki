# CLAUDE.md

This repository is DM1, the data-mining guide, a companion to the software-engineering guide (SE1). It
follows SE1 as a project and publishes its own agreement for data work. Both are imported here so
they are in context at every start, not left in files to be opened
([SE1/A11](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/10-agents.md#a11)):

@docs/SE1-AGENTS.md
@vendor/se1/QUICKREF.md
@AGENTS.md

`docs/SE1-AGENTS.md` is SE1's AGENTS.md copied unedited from v1.7, and `vendor/se1/` is all of SE1 at that release, whose `QUICKREF.md` is imported here; a grep for `SE1/D5.` there lands on the definition. The root `AGENTS.md` is the file this
guide publishes, so SE1's copy cannot sit at the root here; adopters' names are in the README.

How to run and check this repository, and what AGENTS.md's nouns mean here, is in
[CONTRIBUTING.md](CONTRIBUTING.md). `./se --post` runs at every start as a hook; `./dm --doctor` says
what the repository lacks.
