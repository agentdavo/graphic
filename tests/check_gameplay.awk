# Check observable outcomes, independent of shader output and trace hashes.
NR == 1 {
    if ($1 != "vkmin-state-v1") exit 1
    for (i = 4; i <= NF; ++i) field[$i] = i - 1
    next
}
{
    rows++
    if ($1 != rows - 1) bad = 1
    for (name in field) value[name] = $(field[name])
    if (game == "14_anime") animation[value["animation"]] = 1
}
END {
    if (!rows || bad) exit 1
    if (game == "10_shooter" && (value["door"] != 45 || value["shots"] != 6 || value["hits"] != 6 || value["target0"] != 3 || value["target1"] != 3)) exit 1
    if (game == "11_rts" && (value["orders"] != 1 || value["selected"] < 1 || value["tick"] != 150)) exit 1
    if (game == "12_topdown" && (value["selections"] < 1 || value["tick"] != 150)) exit 1
    if (game == "13_platformer" && (value["jumps"] < 2 || value["landings"] < 2 || value["tick"] != 300)) exit 1
    if (game == "14_anime" && (!animation[0] || !animation[1] || !animation[2])) exit 1
    print "  gameplay: " game " (" rows " frames)"
}
