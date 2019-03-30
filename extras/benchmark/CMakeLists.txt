# Written in 2017 by Henrik Steffen Gaßmann <henrik@gassmann.onl>
#
# To the extent possible under law, the author(s) have dedicated all
# copyright and related and neighboring rights to this software to the
# public domain worldwide. This software is distributed without any warranty.
#
# You should have received a copy of the CC0 Public Domain Dedication
# along with this software. If not, see
#
#     http://creativecommons.org/publicdomain/zero/1.0/
#
########################################################################

add_executable(vefs-bench
    src/benchmarks.cpp
)
target_link_libraries(vefs-bench
    PUBLIC
        benchmark::benchmark
        vefs
        shlwapi
)

