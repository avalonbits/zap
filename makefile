# Copyright (C) 2023  Igor Cananea <icc@avalonbits.com>
# Author: Igor Cananea <icc@avalonbits.com>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

# ----------------------------
# Makefile Options
# ----------------------------

NAME = zap
ICON = icon.png
DESCRIPTION = "eZ80 Assembler Program."
COMPRESSED = NO
ARCHIVED = NO
INIT_LOC = 0B0000
BSSHEAP_LOW = 040000
BSSHEAP_HIGH = 0A7FFF
STACK_HIGH = 0AFFFF

CFLAGS = -Werror -Wall -Wextra -Oz
CXXFLAGS = -Werror -Wall -Wextra -Oz

# ----------------------------

include $(shell cedev-config --makefile)
