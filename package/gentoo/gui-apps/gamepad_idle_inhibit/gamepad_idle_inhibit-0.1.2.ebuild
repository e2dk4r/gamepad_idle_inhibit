# Copyright 1999-2023 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

inherit meson

DESCRIPTION="Prevent idling on gamepad events on wayland"
HOMEPAGE="https://github.com/e2dk4r/gamepad_idle_inhibit"

if [[ ${PV} == 9999 ]]; then
	inherit git-r3
	EGIT_REPO_URI="https://github.com/e2dk4r/${PN}.git"
else
	SRC_URI="https://github.com/e2dk4r/${PN}/archive/v${PV}.tar.gz -> ${P}.tar.gz"
	KEYWORDS="amd64 arm64 ~loong ~ppc64 ~riscv x86"
fi

LICENSE="MIT"
SLOT="0"

DEPEND="
  dev-libs/wayland
  sys-libs/liburing
"
RDEPEND="${DEPEND}"
BDEPEND="
	>=dev-libs/wayland-protocols-1.27
	virtual/pkgconfig
"

src_configure() {
	meson_src_configure
}
