# cosmocc.mk — the ONE place the Cosmopolitan toolchain is pinned.
# Used by the Makefile (`make toolchain`) and the CI setup-cosmocc action.
#
# COSMOCC_SHA256 is the sha256 of cosmocc-$(COSMOCC_VERSION).zip
# (441,763,966 bytes), the asset of the jart/cosmopolitan GitHub release
# https://github.com/jart/cosmopolitan/releases/tag/4.0.2 — also served from
# https://cosmo.zip/pub/cosmocc/cosmocc-4.0.2.zip. Measured 2026-09-25.
# COSMO_SRC_COMMIT is the commit that release was built from (tag 4.0.2);
# the vendors/submodules/cosmopolitan gitlink points at the same commit.
COSMOCC_VERSION := 4.0.2
COSMOCC_SHA256 := 85b8c37a406d862e656ad4ec14be9f6ce474c1b436b9615e91a55208aced3f44
COSMO_SRC_COMMIT := 5907304049f37c9ed77593974d13202829443bea
