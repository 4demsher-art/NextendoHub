# GitHub build

The included `.github/workflows/build.yml` builds `NextendoHub.nro`.
It runs `setup.sh` first to fetch and stage the Borealis dependency, then runs `make`.
The finished NRO is uploaded as the `NextendoHub-NRO` Actions artifact.
