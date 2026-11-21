/* ============================================================================
 * typst.js — Faithful JavaScript Port of typst.c (2026 HUGO)
 * Zero-dependency Typst-like Parser, Interactive Canvas Viewer & PDF 1.4 Engine
 * Public Domain
 * ============================================================================ */
const PDF417_CLUSTERS = [
   [0x1d5c0, 0x1eaf0, 0x1f57c, 0x1d4e0, 0x1ea78, 0x1f53e,
          0x1a8c0, 0x1d470, 0x1a860, 0x15040, 0x1a830, 0x15020,
          0x1adc0, 0x1d6f0, 0x1eb7c, 0x1ace0, 0x1d678, 0x1eb3e,
          0x158c0, 0x1ac70, 0x15860, 0x15dc0, 0x1aef0, 0x1d77c,
          0x15ce0, 0x1ae78, 0x1d73e, 0x15c70, 0x1ae3c, 0x15ef0,
          0x1af7c, 0x15e78, 0x1af3e, 0x15f7c, 0x1f5fa, 0x1d2e0,
          0x1e978, 0x1f4be, 0x1a4c0, 0x1d270, 0x1e93c, 0x1a460,
          0x1d238, 0x14840, 0x1a430, 0x1d21c, 0x14820, 0x1a418,
          0x14810, 0x1a6e0, 0x1d378, 0x1e9be, 0x14cc0, 0x1a670,
          0x1d33c, 0x14c60, 0x1a638, 0x1d31e, 0x14c30, 0x1a61c,
          0x14ee0, 0x1a778, 0x1d3be, 0x14e70, 0x1a73c, 0x14e38,
          0x1a71e, 0x14f78, 0x1a7be, 0x14f3c, 0x14f1e, 0x1a2c0,
          0x1d170, 0x1e8bc, 0x1a260, 0x1d138, 0x1e89e, 0x14440,
          0x1a230, 0x1d11c, 0x14420, 0x1a218, 0x14410, 0x14408,
          0x146c0, 0x1a370, 0x1d1bc, 0x14660, 0x1a338, 0x1d19e,
          0x14630, 0x1a31c, 0x14618, 0x1460c, 0x14770, 0x1a3bc,
          0x14738, 0x1a39e, 0x1471c, 0x147bc, 0x1a160, 0x1d0b8,
          0x1e85e, 0x14240, 0x1a130, 0x1d09c, 0x14220, 0x1a118,
          0x1d08e, 0x14210, 0x1a10c, 0x14208, 0x1a106, 0x14360,
          0x1a1b8, 0x1d0de, 0x14330, 0x1a19c, 0x14318, 0x1a18e,
          0x1430c, 0x14306, 0x1a1de, 0x1438e, 0x14140, 0x1a0b0,
          0x1d05c, 0x14120, 0x1a098, 0x1d04e, 0x14110, 0x1a08c,
          0x14108, 0x1a086, 0x14104, 0x141b0, 0x14198, 0x1418c,
          0x140a0, 0x1d02e, 0x1a04c, 0x1a046, 0x14082, 0x1cae0,
          0x1e578, 0x1f2be, 0x194c0, 0x1ca70, 0x1e53c, 0x19460,
          0x1ca38, 0x1e51e, 0x12840, 0x19430, 0x12820, 0x196e0,
          0x1cb78, 0x1e5be, 0x12cc0, 0x19670, 0x1cb3c, 0x12c60,
          0x19638, 0x12c30, 0x12c18, 0x12ee0, 0x19778, 0x1cbbe,
          0x12e70, 0x1973c, 0x12e38, 0x12e1c, 0x12f78, 0x197be,
          0x12f3c, 0x12fbe, 0x1dac0, 0x1ed70, 0x1f6bc, 0x1da60,
          0x1ed38, 0x1f69e, 0x1b440, 0x1da30, 0x1ed1c, 0x1b420,
          0x1da18, 0x1ed0e, 0x1b410, 0x1da0c, 0x192c0, 0x1c970,
          0x1e4bc, 0x1b6c0, 0x19260, 0x1c938, 0x1e49e, 0x1b660,
          0x1db38, 0x1ed9e, 0x16c40, 0x12420, 0x19218, 0x1c90e,
          0x16c20, 0x1b618, 0x16c10, 0x126c0, 0x19370, 0x1c9bc,
          0x16ec0, 0x12660, 0x19338, 0x1c99e, 0x16e60, 0x1b738,
          0x1db9e, 0x16e30, 0x12618, 0x16e18, 0x12770, 0x193bc,
          0x16f70, 0x12738, 0x1939e, 0x16f38, 0x1b79e, 0x16f1c,
          0x127bc, 0x16fbc, 0x1279e, 0x16f9e, 0x1d960, 0x1ecb8,
          0x1f65e, 0x1b240, 0x1d930, 0x1ec9c, 0x1b220, 0x1d918,
          0x1ec8e, 0x1b210, 0x1d90c, 0x1b208, 0x1b204, 0x19160,
          0x1c8b8, 0x1e45e, 0x1b360, 0x19130, 0x1c89c, 0x16640,
          0x12220, 0x1d99c, 0x1c88e, 0x16620, 0x12210, 0x1910c,
          0x16610, 0x1b30c, 0x19106, 0x12204, 0x12360, 0x191b8,
          0x1c8de, 0x16760, 0x12330, 0x1919c, 0x16730, 0x1b39c,
          0x1918e, 0x16718, 0x1230c, 0x12306, 0x123b8, 0x191de,
          0x167b8, 0x1239c, 0x1679c, 0x1238e, 0x1678e, 0x167de,
          0x1b140, 0x1d8b0, 0x1ec5c, 0x1b120, 0x1d898, 0x1ec4e,
          0x1b110, 0x1d88c, 0x1b108, 0x1d886, 0x1b104, 0x1b102,
          0x12140, 0x190b0, 0x1c85c, 0x16340, 0x12120, 0x19098,
          0x1c84e, 0x16320, 0x1b198, 0x1d8ce, 0x16310, 0x12108,
          0x19086, 0x16308, 0x1b186, 0x16304, 0x121b0, 0x190dc,
          0x163b0, 0x12198, 0x190ce, 0x16398, 0x1b1ce, 0x1638c,
          0x12186, 0x16386, 0x163dc, 0x163ce, 0x1b0a0, 0x1d858,
          0x1ec2e, 0x1b090, 0x1d84c, 0x1b088, 0x1d846, 0x1b084,
          0x1b082, 0x120a0, 0x19058, 0x1c82e, 0x161a0, 0x12090,
          0x1904c, 0x16190, 0x1b0cc, 0x19046, 0x16188, 0x12084,
          0x16184, 0x12082, 0x120d8, 0x161d8, 0x161cc, 0x161c6,
          0x1d82c, 0x1d826, 0x1b042, 0x1902c, 0x12048, 0x160c8,
          0x160c4, 0x160c2, 0x18ac0, 0x1c570, 0x1e2bc, 0x18a60,
          0x1c538, 0x11440, 0x18a30, 0x1c51c, 0x11420, 0x18a18,
          0x11410, 0x11408, 0x116c0, 0x18b70, 0x1c5bc, 0x11660,
          0x18b38, 0x1c59e, 0x11630, 0x18b1c, 0x11618, 0x1160c,
          0x11770, 0x18bbc, 0x11738, 0x18b9e, 0x1171c, 0x117bc,
          0x1179e, 0x1cd60, 0x1e6b8, 0x1f35e, 0x19a40, 0x1cd30,
          0x1e69c, 0x19a20, 0x1cd18, 0x1e68e, 0x19a10, 0x1cd0c,
          0x19a08, 0x1cd06, 0x18960, 0x1c4b8, 0x1e25e, 0x19b60,
          0x18930, 0x1c49c, 0x13640, 0x11220, 0x1cd9c, 0x1c48e,
          0x13620, 0x19b18, 0x1890c, 0x13610, 0x11208, 0x13608,
          0x11360, 0x189b8, 0x1c4de, 0x13760, 0x11330, 0x1cdde,
          0x13730, 0x19b9c, 0x1898e, 0x13718, 0x1130c, 0x1370c,
          0x113b8, 0x189de, 0x137b8, 0x1139c, 0x1379c, 0x1138e,
          0x113de, 0x137de, 0x1dd40, 0x1eeb0, 0x1f75c, 0x1dd20,
          0x1ee98, 0x1f74e, 0x1dd10, 0x1ee8c, 0x1dd08, 0x1ee86,
          0x1dd04, 0x19940, 0x1ccb0, 0x1e65c, 0x1bb40, 0x19920,
          0x1eedc, 0x1e64e, 0x1bb20, 0x1dd98, 0x1eece, 0x1bb10,
          0x19908, 0x1cc86, 0x1bb08, 0x1dd86, 0x19902, 0x11140,
          0x188b0, 0x1c45c, 0x13340, 0x11120, 0x18898, 0x1c44e,
          0x17740, 0x13320, 0x19998, 0x1ccce, 0x17720, 0x1bb98,
          0x1ddce, 0x18886, 0x17710, 0x13308, 0x19986, 0x17708,
          0x11102, 0x111b0, 0x188dc, 0x133b0, 0x11198, 0x188ce,
          0x177b0, 0x13398, 0x199ce, 0x17798, 0x1bbce, 0x11186,
          0x13386, 0x111dc, 0x133dc, 0x111ce, 0x177dc, 0x133ce,
          0x1dca0, 0x1ee58, 0x1f72e, 0x1dc90, 0x1ee4c, 0x1dc88,
          0x1ee46, 0x1dc84, 0x1dc82, 0x198a0, 0x1cc58, 0x1e62e,
          0x1b9a0, 0x19890, 0x1ee6e, 0x1b990, 0x1dccc, 0x1cc46,
          0x1b988, 0x19884, 0x1b984, 0x19882, 0x1b982, 0x110a0,
          0x18858, 0x1c42e, 0x131a0, 0x11090, 0x1884c, 0x173a0,
          0x13190, 0x198cc, 0x18846, 0x17390, 0x1b9cc, 0x11084,
          0x17388, 0x13184, 0x11082, 0x13182, 0x110d8, 0x1886e,
          0x131d8, 0x110cc, 0x173d8, 0x131cc, 0x110c6, 0x173cc,
          0x131c6, 0x110ee, 0x173ee, 0x1dc50, 0x1ee2c, 0x1dc48,
          0x1ee26, 0x1dc44, 0x1dc42, 0x19850, 0x1cc2c, 0x1b8d0,
          0x19848, 0x1cc26, 0x1b8c8, 0x1dc66, 0x1b8c4, 0x19842,
          0x1b8c2, 0x11050, 0x1882c, 0x130d0, 0x11048, 0x18826,
          0x171d0, 0x130c8, 0x19866, 0x171c8, 0x1b8e6, 0x11042,
          0x171c4, 0x130c2, 0x171c2, 0x130ec, 0x171ec, 0x171e6,
          0x1ee16, 0x1dc22, 0x1cc16, 0x19824, 0x19822, 0x11028,
          0x13068, 0x170e8, 0x11022, 0x13062, 0x18560, 0x10a40,
          0x18530, 0x10a20, 0x18518, 0x1c28e, 0x10a10, 0x1850c,
          0x10a08, 0x18506, 0x10b60, 0x185b8, 0x1c2de, 0x10b30,
          0x1859c, 0x10b18, 0x1858e, 0x10b0c, 0x10b06, 0x10bb8,
          0x185de, 0x10b9c, 0x10b8e, 0x10bde, 0x18d40, 0x1c6b0,
          0x1e35c, 0x18d20, 0x1c698, 0x18d10, 0x1c68c, 0x18d08,
          0x1c686, 0x18d04, 0x10940, 0x184b0, 0x1c25c, 0x11b40,
          0x10920, 0x1c6dc, 0x1c24e, 0x11b20, 0x18d98, 0x1c6ce,
          0x11b10, 0x10908, 0x18486, 0x11b08, 0x18d86, 0x10902,
          0x109b0, 0x184dc, 0x11bb0, 0x10998, 0x184ce, 0x11b98,
          0x18dce, 0x11b8c, 0x10986, 0x109dc, 0x11bdc, 0x109ce,
          0x11bce, 0x1cea0, 0x1e758, 0x1f3ae, 0x1ce90, 0x1e74c,
          0x1ce88, 0x1e746, 0x1ce84, 0x1ce82, 0x18ca0, 0x1c658,
          0x19da0, 0x18c90, 0x1c64c, 0x19d90, 0x1cecc, 0x1c646,
          0x19d88, 0x18c84, 0x19d84, 0x18c82, 0x19d82, 0x108a0,
          0x18458, 0x119a0, 0x10890, 0x1c66e, 0x13ba0, 0x11990,
          0x18ccc, 0x18446, 0x13b90, 0x19dcc, 0x10884, 0x13b88,
          0x11984, 0x10882, 0x11982, 0x108d8, 0x1846e, 0x119d8,
          0x108cc, 0x13bd8, 0x119cc, 0x108c6, 0x13bcc, 0x119c6,
          0x108ee, 0x119ee, 0x13bee, 0x1ef50, 0x1f7ac, 0x1ef48,
          0x1f7a6, 0x1ef44, 0x1ef42, 0x1ce50, 0x1e72c, 0x1ded0,
          0x1ef6c, 0x1e726, 0x1dec8, 0x1ef66, 0x1dec4, 0x1ce42,
          0x1dec2, 0x18c50, 0x1c62c, 0x19cd0, 0x18c48, 0x1c626,
          0x1bdd0, 0x19cc8, 0x1ce66, 0x1bdc8, 0x1dee6, 0x18c42,
          0x1bdc4, 0x19cc2, 0x1bdc2, 0x10850, 0x1842c, 0x118d0,
          0x10848, 0x18426, 0x139d0, 0x118c8, 0x18c66, 0x17bd0,
          0x139c8, 0x19ce6, 0x10842, 0x17bc8, 0x1bde6, 0x118c2,
          0x17bc4, 0x1086c, 0x118ec, 0x10866, 0x139ec, 0x118e6,
          0x17bec, 0x139e6, 0x17be6, 0x1ef28, 0x1f796, 0x1ef24,
          0x1ef22, 0x1ce28, 0x1e716, 0x1de68, 0x1ef36, 0x1de64,
          0x1ce22, 0x1de62, 0x18c28, 0x1c616, 0x19c68, 0x18c24,
          0x1bce8, 0x19c64, 0x18c22, 0x1bce4, 0x19c62, 0x1bce2,
          0x10828, 0x18416, 0x11868, 0x18c36, 0x138e8, 0x11864,
          0x10822, 0x179e8, 0x138e4, 0x11862, 0x179e4, 0x138e2,
          0x179e2, 0x11876, 0x179f6, 0x1ef12, 0x1de34, 0x1de32,
          0x19c34, 0x1bc74, 0x1bc72, 0x11834, 0x13874, 0x178f4,
          0x178f2, 0x10540, 0x10520, 0x18298, 0x10510, 0x10508,
          0x10504, 0x105b0, 0x10598, 0x1058c, 0x10586, 0x105dc,
          0x105ce, 0x186a0, 0x18690, 0x1c34c, 0x18688, 0x1c346,
          0x18684, 0x18682, 0x104a0, 0x18258, 0x10da0, 0x186d8,
          0x1824c, 0x10d90, 0x186cc, 0x10d88, 0x186c6, 0x10d84,
          0x10482, 0x10d82, 0x104d8, 0x1826e, 0x10dd8, 0x186ee,
          0x10dcc, 0x104c6, 0x10dc6, 0x104ee, 0x10dee, 0x1c750,
          0x1c748, 0x1c744, 0x1c742, 0x18650, 0x18ed0, 0x1c76c,
          0x1c326, 0x18ec8, 0x1c766, 0x18ec4, 0x18642, 0x18ec2,
          0x10450, 0x10cd0, 0x10448, 0x18226, 0x11dd0, 0x10cc8,
          0x10444, 0x11dc8, 0x10cc4, 0x10442, 0x11dc4, 0x10cc2,
          0x1046c, 0x10cec, 0x10466, 0x11dec, 0x10ce6, 0x11de6,
          0x1e7a8, 0x1e7a4, 0x1e7a2, 0x1c728, 0x1cf68, 0x1e7b6,
          0x1cf64, 0x1c722, 0x1cf62, 0x18628, 0x1c316, 0x18e68,
          0x1c736, 0x19ee8, 0x18e64, 0x18622, 0x19ee4, 0x18e62,
          0x19ee2, 0x10428, 0x18216, 0x10c68, 0x18636, 0x11ce8,
          0x10c64, 0x10422, 0x13de8, 0x11ce4, 0x10c62, 0x13de4,
          0x11ce2, 0x10436, 0x10c76, 0x11cf6, 0x13df6, 0x1f7d4,
          0x1f7d2, 0x1e794, 0x1efb4, 0x1e792, 0x1efb2, 0x1c714,
          0x1cf34, 0x1c712, 0x1df74, 0x1cf32, 0x1df72, 0x18614,
          0x18e34, 0x18612, 0x19e74, 0x18e32, 0x1bef4],
      [0x1f560, 0x1fab8, 0x1ea40, 0x1f530, 0x1fa9c, 0x1ea20,
          0x1f518, 0x1fa8e, 0x1ea10, 0x1f50c, 0x1ea08, 0x1f506,
          0x1ea04, 0x1eb60, 0x1f5b8, 0x1fade, 0x1d640, 0x1eb30,
          0x1f59c, 0x1d620, 0x1eb18, 0x1f58e, 0x1d610, 0x1eb0c,
          0x1d608, 0x1eb06, 0x1d604, 0x1d760, 0x1ebb8, 0x1f5de,
          0x1ae40, 0x1d730, 0x1eb9c, 0x1ae20, 0x1d718, 0x1eb8e,
          0x1ae10, 0x1d70c, 0x1ae08, 0x1d706, 0x1ae04, 0x1af60,
          0x1d7b8, 0x1ebde, 0x15e40, 0x1af30, 0x1d79c, 0x15e20,
          0x1af18, 0x1d78e, 0x15e10, 0x1af0c, 0x15e08, 0x1af06,
          0x15f60, 0x1afb8, 0x1d7de, 0x15f30, 0x1af9c, 0x15f18,
          0x1af8e, 0x15f0c, 0x15fb8, 0x1afde, 0x15f9c, 0x15f8e,
          0x1e940, 0x1f4b0, 0x1fa5c, 0x1e920, 0x1f498, 0x1fa4e,
          0x1e910, 0x1f48c, 0x1e908, 0x1f486, 0x1e904, 0x1e902,
          0x1d340, 0x1e9b0, 0x1f4dc, 0x1d320, 0x1e998, 0x1f4ce,
          0x1d310, 0x1e98c, 0x1d308, 0x1e986, 0x1d304, 0x1d302,
          0x1a740, 0x1d3b0, 0x1e9dc, 0x1a720, 0x1d398, 0x1e9ce,
          0x1a710, 0x1d38c, 0x1a708, 0x1d386, 0x1a704, 0x1a702,
          0x14f40, 0x1a7b0, 0x1d3dc, 0x14f20, 0x1a798, 0x1d3ce,
          0x14f10, 0x1a78c, 0x14f08, 0x1a786, 0x14f04, 0x14fb0,
          0x1a7dc, 0x14f98, 0x1a7ce, 0x14f8c, 0x14f86, 0x14fdc,
          0x14fce, 0x1e8a0, 0x1f458, 0x1fa2e, 0x1e890, 0x1f44c,
          0x1e888, 0x1f446, 0x1e884, 0x1e882, 0x1d1a0, 0x1e8d8,
          0x1f46e, 0x1d190, 0x1e8cc, 0x1d188, 0x1e8c6, 0x1d184,
          0x1d182, 0x1a3a0, 0x1d1d8, 0x1e8ee, 0x1a390, 0x1d1cc,
          0x1a388, 0x1d1c6, 0x1a384, 0x1a382, 0x147a0, 0x1a3d8,
          0x1d1ee, 0x14790, 0x1a3cc, 0x14788, 0x1a3c6, 0x14784,
          0x14782, 0x147d8, 0x1a3ee, 0x147cc, 0x147c6, 0x147ee,
          0x1e850, 0x1f42c, 0x1e848, 0x1f426, 0x1e844, 0x1e842,
          0x1d0d0, 0x1e86c, 0x1d0c8, 0x1e866, 0x1d0c4, 0x1d0c2,
          0x1a1d0, 0x1d0ec, 0x1a1c8, 0x1d0e6, 0x1a1c4, 0x1a1c2,
          0x143d0, 0x1a1ec, 0x143c8, 0x1a1e6, 0x143c4, 0x143c2,
          0x143ec, 0x143e6, 0x1e828, 0x1f416, 0x1e824, 0x1e822,
          0x1d068, 0x1e836, 0x1d064, 0x1d062, 0x1a0e8, 0x1d076,
          0x1a0e4, 0x1a0e2, 0x141e8, 0x1a0f6, 0x141e4, 0x141e2,
          0x1e814, 0x1e812, 0x1d034, 0x1d032, 0x1a074, 0x1a072,
          0x1e540, 0x1f2b0, 0x1f95c, 0x1e520, 0x1f298, 0x1f94e,
          0x1e510, 0x1f28c, 0x1e508, 0x1f286, 0x1e504, 0x1e502,
          0x1cb40, 0x1e5b0, 0x1f2dc, 0x1cb20, 0x1e598, 0x1f2ce,
          0x1cb10, 0x1e58c, 0x1cb08, 0x1e586, 0x1cb04, 0x1cb02,
          0x19740, 0x1cbb0, 0x1e5dc, 0x19720, 0x1cb98, 0x1e5ce,
          0x19710, 0x1cb8c, 0x19708, 0x1cb86, 0x19704, 0x19702,
          0x12f40, 0x197b0, 0x1cbdc, 0x12f20, 0x19798, 0x1cbce,
          0x12f10, 0x1978c, 0x12f08, 0x19786, 0x12f04, 0x12fb0,
          0x197dc, 0x12f98, 0x197ce, 0x12f8c, 0x12f86, 0x12fdc,
          0x12fce, 0x1f6a0, 0x1fb58, 0x16bf0, 0x1f690, 0x1fb4c,
          0x169f8, 0x1f688, 0x1fb46, 0x168fc, 0x1f684, 0x1f682,
          0x1e4a0, 0x1f258, 0x1f92e, 0x1eda0, 0x1e490, 0x1fb6e,
          0x1ed90, 0x1f6cc, 0x1f246, 0x1ed88, 0x1e484, 0x1ed84,
          0x1e482, 0x1ed82, 0x1c9a0, 0x1e4d8, 0x1f26e, 0x1dba0,
          0x1c990, 0x1e4cc, 0x1db90, 0x1edcc, 0x1e4c6, 0x1db88,
          0x1c984, 0x1db84, 0x1c982, 0x1db82, 0x193a0, 0x1c9d8,
          0x1e4ee, 0x1b7a0, 0x19390, 0x1c9cc, 0x1b790, 0x1dbcc,
          0x1c9c6, 0x1b788, 0x19384, 0x1b784, 0x19382, 0x1b782,
          0x127a0, 0x193d8, 0x1c9ee, 0x16fa0, 0x12790, 0x193cc,
          0x16f90, 0x1b7cc, 0x193c6, 0x16f88, 0x12784, 0x16f84,
          0x12782, 0x127d8, 0x193ee, 0x16fd8, 0x127cc, 0x16fcc,
          0x127c6, 0x16fc6, 0x127ee, 0x1f650, 0x1fb2c, 0x165f8,
          0x1f648, 0x1fb26, 0x164fc, 0x1f644, 0x1647e, 0x1f642,
          0x1e450, 0x1f22c, 0x1ecd0, 0x1e448, 0x1f226, 0x1ecc8,
          0x1f666, 0x1ecc4, 0x1e442, 0x1ecc2, 0x1c8d0, 0x1e46c,
          0x1d9d0, 0x1c8c8, 0x1e466, 0x1d9c8, 0x1ece6, 0x1d9c4,
          0x1c8c2, 0x1d9c2, 0x191d0, 0x1c8ec, 0x1b3d0, 0x191c8,
          0x1c8e6, 0x1b3c8, 0x1d9e6, 0x1b3c4, 0x191c2, 0x1b3c2,
          0x123d0, 0x191ec, 0x167d0, 0x123c8, 0x191e6, 0x167c8,
          0x1b3e6, 0x167c4, 0x123c2, 0x167c2, 0x123ec, 0x167ec,
          0x123e6, 0x167e6, 0x1f628, 0x1fb16, 0x162fc, 0x1f624,
          0x1627e, 0x1f622, 0x1e428, 0x1f216, 0x1ec68, 0x1f636,
          0x1ec64, 0x1e422, 0x1ec62, 0x1c868, 0x1e436, 0x1d8e8,
          0x1c864, 0x1d8e4, 0x1c862, 0x1d8e2, 0x190e8, 0x1c876,
          0x1b1e8, 0x1d8f6, 0x1b1e4, 0x190e2, 0x1b1e2, 0x121e8,
          0x190f6, 0x163e8, 0x121e4, 0x163e4, 0x121e2, 0x163e2,
          0x121f6, 0x163f6, 0x1f614, 0x1617e, 0x1f612, 0x1e414,
          0x1ec34, 0x1e412, 0x1ec32, 0x1c834, 0x1d874, 0x1c832,
          0x1d872, 0x19074, 0x1b0f4, 0x19072, 0x1b0f2, 0x120f4,
          0x161f4, 0x120f2, 0x161f2, 0x1f60a, 0x1e40a, 0x1ec1a,
          0x1c81a, 0x1d83a, 0x1903a, 0x1b07a, 0x1e2a0, 0x1f158,
          0x1f8ae, 0x1e290, 0x1f14c, 0x1e288, 0x1f146, 0x1e284,
          0x1e282, 0x1c5a0, 0x1e2d8, 0x1f16e, 0x1c590, 0x1e2cc,
          0x1c588, 0x1e2c6, 0x1c584, 0x1c582, 0x18ba0, 0x1c5d8,
          0x1e2ee, 0x18b90, 0x1c5cc, 0x18b88, 0x1c5c6, 0x18b84,
          0x18b82, 0x117a0, 0x18bd8, 0x1c5ee, 0x11790, 0x18bcc,
          0x11788, 0x18bc6, 0x11784, 0x11782, 0x117d8, 0x18bee,
          0x117cc, 0x117c6, 0x117ee, 0x1f350, 0x1f9ac, 0x135f8,
          0x1f348, 0x1f9a6, 0x134fc, 0x1f344, 0x1347e, 0x1f342,
          0x1e250, 0x1f12c, 0x1e6d0, 0x1e248, 0x1f126, 0x1e6c8,
          0x1f366, 0x1e6c4, 0x1e242, 0x1e6c2, 0x1c4d0, 0x1e26c,
          0x1cdd0, 0x1c4c8, 0x1e266, 0x1cdc8, 0x1e6e6, 0x1cdc4,
          0x1c4c2, 0x1cdc2, 0x189d0, 0x1c4ec, 0x19bd0, 0x189c8,
          0x1c4e6, 0x19bc8, 0x1cde6, 0x19bc4, 0x189c2, 0x19bc2,
          0x113d0, 0x189ec, 0x137d0, 0x113c8, 0x189e6, 0x137c8,
          0x19be6, 0x137c4, 0x113c2, 0x137c2, 0x113ec, 0x137ec,
          0x113e6, 0x137e6, 0x1fba8, 0x175f0, 0x1bafc, 0x1fba4,
          0x174f8, 0x1ba7e, 0x1fba2, 0x1747c, 0x1743e, 0x1f328,
          0x1f996, 0x132fc, 0x1f768, 0x1fbb6, 0x176fc, 0x1327e,
          0x1f764, 0x1f322, 0x1767e, 0x1f762, 0x1e228, 0x1f116,
          0x1e668, 0x1e224, 0x1eee8, 0x1f776, 0x1e222, 0x1eee4,
          0x1e662, 0x1eee2, 0x1c468, 0x1e236, 0x1cce8, 0x1c464,
          0x1dde8, 0x1cce4, 0x1c462, 0x1dde4, 0x1cce2, 0x1dde2,
          0x188e8, 0x1c476, 0x199e8, 0x188e4, 0x1bbe8, 0x199e4,
          0x188e2, 0x1bbe4, 0x199e2, 0x1bbe2, 0x111e8, 0x188f6,
          0x133e8, 0x111e4, 0x177e8, 0x133e4, 0x111e2, 0x177e4,
          0x133e2, 0x177e2, 0x111f6, 0x133f6, 0x1fb94, 0x172f8,
          0x1b97e, 0x1fb92, 0x1727c, 0x1723e, 0x1f314, 0x1317e,
          0x1f734, 0x1f312, 0x1737e, 0x1f732, 0x1e214, 0x1e634,
          0x1e212, 0x1ee74, 0x1e632, 0x1ee72, 0x1c434, 0x1cc74,
          0x1c432, 0x1dcf4, 0x1cc72, 0x1dcf2, 0x18874, 0x198f4,
          0x18872, 0x1b9f4, 0x198f2, 0x1b9f2, 0x110f4, 0x131f4,
          0x110f2, 0x173f4, 0x131f2, 0x173f2, 0x1fb8a, 0x1717c,
          0x1713e, 0x1f30a, 0x1f71a, 0x1e20a, 0x1e61a, 0x1ee3a,
          0x1c41a, 0x1cc3a, 0x1dc7a, 0x1883a, 0x1987a, 0x1b8fa,
          0x1107a, 0x130fa, 0x171fa, 0x170be, 0x1e150, 0x1f0ac,
          0x1e148, 0x1f0a6, 0x1e144, 0x1e142, 0x1c2d0, 0x1e16c,
          0x1c2c8, 0x1e166, 0x1c2c4, 0x1c2c2, 0x185d0, 0x1c2ec,
          0x185c8, 0x1c2e6, 0x185c4, 0x185c2, 0x10bd0, 0x185ec,
          0x10bc8, 0x185e6, 0x10bc4, 0x10bc2, 0x10bec, 0x10be6,
          0x1f1a8, 0x1f8d6, 0x11afc, 0x1f1a4, 0x11a7e, 0x1f1a2,
          0x1e128, 0x1f096, 0x1e368, 0x1e124, 0x1e364, 0x1e122,
          0x1e362, 0x1c268, 0x1e136, 0x1c6e8, 0x1c264, 0x1c6e4,
          0x1c262, 0x1c6e2, 0x184e8, 0x1c276, 0x18de8, 0x184e4,
          0x18de4, 0x184e2, 0x18de2, 0x109e8, 0x184f6, 0x11be8,
          0x109e4, 0x11be4, 0x109e2, 0x11be2, 0x109f6, 0x11bf6,
          0x1f9d4, 0x13af8, 0x19d7e, 0x1f9d2, 0x13a7c, 0x13a3e,
          0x1f194, 0x1197e, 0x1f3b4, 0x1f192, 0x13b7e, 0x1f3b2,
          0x1e114, 0x1e334, 0x1e112, 0x1e774, 0x1e332, 0x1e772,
          0x1c234, 0x1c674, 0x1c232, 0x1cef4, 0x1c672, 0x1cef2,
          0x18474, 0x18cf4, 0x18472, 0x19df4, 0x18cf2, 0x19df2,
          0x108f4, 0x119f4, 0x108f2, 0x13bf4, 0x119f2, 0x13bf2,
          0x17af0, 0x1bd7c, 0x17a78, 0x1bd3e, 0x17a3c, 0x17a1e,
          0x1f9ca, 0x1397c, 0x1fbda, 0x17b7c, 0x1393e, 0x17b3e,
          0x1f18a, 0x1f39a, 0x1f7ba, 0x1e10a, 0x1e31a, 0x1e73a,
          0x1ef7a, 0x1c21a, 0x1c63a, 0x1ce7a, 0x1defa, 0x1843a,
          0x18c7a, 0x19cfa, 0x1bdfa, 0x1087a, 0x118fa, 0x139fa,
          0x17978, 0x1bcbe, 0x1793c, 0x1791e, 0x138be, 0x179be,
          0x178bc, 0x1789e, 0x1785e, 0x1e0a8, 0x1e0a4, 0x1e0a2,
          0x1c168, 0x1e0b6, 0x1c164, 0x1c162, 0x182e8, 0x1c176,
          0x182e4, 0x182e2, 0x105e8, 0x182f6, 0x105e4, 0x105e2,
          0x105f6, 0x1f0d4, 0x10d7e, 0x1f0d2, 0x1e094, 0x1e1b4,
          0x1e092, 0x1e1b2, 0x1c134, 0x1c374, 0x1c132, 0x1c372,
          0x18274, 0x186f4, 0x18272, 0x186f2, 0x104f4, 0x10df4,
          0x104f2, 0x10df2, 0x1f8ea, 0x11d7c, 0x11d3e, 0x1f0ca,
          0x1f1da, 0x1e08a, 0x1e19a, 0x1e3ba, 0x1c11a, 0x1c33a,
          0x1c77a, 0x1823a, 0x1867a, 0x18efa, 0x1047a, 0x10cfa,
          0x11dfa, 0x13d78, 0x19ebe, 0x13d3c, 0x13d1e, 0x11cbe,
          0x13dbe, 0x17d70, 0x1bebc, 0x17d38, 0x1be9e, 0x17d1c,
          0x17d0e, 0x13cbc, 0x17dbc, 0x13c9e, 0x17d9e, 0x17cb8,
          0x1be5e, 0x17c9c, 0x17c8e, 0x13c5e, 0x17cde, 0x17c5c,
          0x17c4e, 0x17c2e, 0x1c0b4, 0x1c0b2, 0x18174, 0x18172,
          0x102f4, 0x102f2, 0x1e0da, 0x1c09a, 0x1c1ba, 0x1813a,
          0x1837a, 0x1027a, 0x106fa, 0x10ebe, 0x11ebc, 0x11e9e,
          0x13eb8, 0x19f5e, 0x13e9c, 0x13e8e, 0x11e5e, 0x13ede,
          0x17eb0, 0x1bf5c, 0x17e98, 0x1bf4e, 0x17e8c, 0x17e86,
          0x13e5c, 0x17edc, 0x13e4e, 0x17ece, 0x17e58, 0x1bf2e,
          0x17e4c, 0x17e46, 0x13e2e, 0x17e6e, 0x17e2c, 0x17e26,
          0x10f5e, 0x11f5c, 0x11f4e, 0x13f58, 0x19fae, 0x13f4c,
          0x13f46, 0x11f2e, 0x13f6e, 0x13f2c, 0x13f26],
      [0x1abe0, 0x1d5f8, 0x153c0, 0x1a9f0, 0x1d4fc, 0x151e0,
          0x1a8f8, 0x1d47e, 0x150f0, 0x1a87c, 0x15078, 0x1fad0,
          0x15be0, 0x1adf8, 0x1fac8, 0x159f0, 0x1acfc, 0x1fac4,
          0x158f8, 0x1ac7e, 0x1fac2, 0x1587c, 0x1f5d0, 0x1faec,
          0x15df8, 0x1f5c8, 0x1fae6, 0x15cfc, 0x1f5c4, 0x15c7e,
          0x1f5c2, 0x1ebd0, 0x1f5ec, 0x1ebc8, 0x1f5e6, 0x1ebc4,
          0x1ebc2, 0x1d7d0, 0x1ebec, 0x1d7c8, 0x1ebe6, 0x1d7c4,
          0x1d7c2, 0x1afd0, 0x1d7ec, 0x1afc8, 0x1d7e6, 0x1afc4,
          0x14bc0, 0x1a5f0, 0x1d2fc, 0x149e0, 0x1a4f8, 0x1d27e,
          0x148f0, 0x1a47c, 0x14878, 0x1a43e, 0x1483c, 0x1fa68,
          0x14df0, 0x1a6fc, 0x1fa64, 0x14cf8, 0x1a67e, 0x1fa62,
          0x14c7c, 0x14c3e, 0x1f4e8, 0x1fa76, 0x14efc, 0x1f4e4,
          0x14e7e, 0x1f4e2, 0x1e9e8, 0x1f4f6, 0x1e9e4, 0x1e9e2,
          0x1d3e8, 0x1e9f6, 0x1d3e4, 0x1d3e2, 0x1a7e8, 0x1d3f6,
          0x1a7e4, 0x1a7e2, 0x145e0, 0x1a2f8, 0x1d17e, 0x144f0,
          0x1a27c, 0x14478, 0x1a23e, 0x1443c, 0x1441e, 0x1fa34,
          0x146f8, 0x1a37e, 0x1fa32, 0x1467c, 0x1463e, 0x1f474,
          0x1477e, 0x1f472, 0x1e8f4, 0x1e8f2, 0x1d1f4, 0x1d1f2,
          0x1a3f4, 0x1a3f2, 0x142f0, 0x1a17c, 0x14278, 0x1a13e,
          0x1423c, 0x1421e, 0x1fa1a, 0x1437c, 0x1433e, 0x1f43a,
          0x1e87a, 0x1d0fa, 0x14178, 0x1a0be, 0x1413c, 0x1411e,
          0x141be, 0x140bc, 0x1409e, 0x12bc0, 0x195f0, 0x1cafc,
          0x129e0, 0x194f8, 0x1ca7e, 0x128f0, 0x1947c, 0x12878,
          0x1943e, 0x1283c, 0x1f968, 0x12df0, 0x196fc, 0x1f964,
          0x12cf8, 0x1967e, 0x1f962, 0x12c7c, 0x12c3e, 0x1f2e8,
          0x1f976, 0x12efc, 0x1f2e4, 0x12e7e, 0x1f2e2, 0x1e5e8,
          0x1f2f6, 0x1e5e4, 0x1e5e2, 0x1cbe8, 0x1e5f6, 0x1cbe4,
          0x1cbe2, 0x197e8, 0x1cbf6, 0x197e4, 0x197e2, 0x1b5e0,
          0x1daf8, 0x1ed7e, 0x169c0, 0x1b4f0, 0x1da7c, 0x168e0,
          0x1b478, 0x1da3e, 0x16870, 0x1b43c, 0x16838, 0x1b41e,
          0x1681c, 0x125e0, 0x192f8, 0x1c97e, 0x16de0, 0x124f0,
          0x1927c, 0x16cf0, 0x1b67c, 0x1923e, 0x16c78, 0x1243c,
          0x16c3c, 0x1241e, 0x16c1e, 0x1f934, 0x126f8, 0x1937e,
          0x1fb74, 0x1f932, 0x16ef8, 0x1267c, 0x1fb72, 0x16e7c,
          0x1263e, 0x16e3e, 0x1f274, 0x1277e, 0x1f6f4, 0x1f272,
          0x16f7e, 0x1f6f2, 0x1e4f4, 0x1edf4, 0x1e4f2, 0x1edf2,
          0x1c9f4, 0x1dbf4, 0x1c9f2, 0x1dbf2, 0x193f4, 0x193f2,
          0x165c0, 0x1b2f0, 0x1d97c, 0x164e0, 0x1b278, 0x1d93e,
          0x16470, 0x1b23c, 0x16438, 0x1b21e, 0x1641c, 0x1640e,
          0x122f0, 0x1917c, 0x166f0, 0x12278, 0x1913e, 0x16678,
          0x1b33e, 0x1663c, 0x1221e, 0x1661e, 0x1f91a, 0x1237c,
          0x1fb3a, 0x1677c, 0x1233e, 0x1673e, 0x1f23a, 0x1f67a,
          0x1e47a, 0x1ecfa, 0x1c8fa, 0x1d9fa, 0x191fa, 0x162e0,
          0x1b178, 0x1d8be, 0x16270, 0x1b13c, 0x16238, 0x1b11e,
          0x1621c, 0x1620e, 0x12178, 0x190be, 0x16378, 0x1213c,
          0x1633c, 0x1211e, 0x1631e, 0x121be, 0x163be, 0x16170,
          0x1b0bc, 0x16138, 0x1b09e, 0x1611c, 0x1610e, 0x120bc,
          0x161bc, 0x1209e, 0x1619e, 0x160b8, 0x1b05e, 0x1609c,
          0x1608e, 0x1205e, 0x160de, 0x1605c, 0x1604e, 0x115e0,
          0x18af8, 0x1c57e, 0x114f0, 0x18a7c, 0x11478, 0x18a3e,
          0x1143c, 0x1141e, 0x1f8b4, 0x116f8, 0x18b7e, 0x1f8b2,
          0x1167c, 0x1163e, 0x1f174, 0x1177e, 0x1f172, 0x1e2f4,
          0x1e2f2, 0x1c5f4, 0x1c5f2, 0x18bf4, 0x18bf2, 0x135c0,
          0x19af0, 0x1cd7c, 0x134e0, 0x19a78, 0x1cd3e, 0x13470,
          0x19a3c, 0x13438, 0x19a1e, 0x1341c, 0x1340e, 0x112f0,
          0x1897c, 0x136f0, 0x11278, 0x1893e, 0x13678, 0x19b3e,
          0x1363c, 0x1121e, 0x1361e, 0x1f89a, 0x1137c, 0x1f9ba,
          0x1377c, 0x1133e, 0x1373e, 0x1f13a, 0x1f37a, 0x1e27a,
          0x1e6fa, 0x1c4fa, 0x1cdfa, 0x189fa, 0x1bae0, 0x1dd78,
          0x1eebe, 0x174c0, 0x1ba70, 0x1dd3c, 0x17460, 0x1ba38,
          0x1dd1e, 0x17430, 0x1ba1c, 0x17418, 0x1ba0e, 0x1740c,
          0x132e0, 0x19978, 0x1ccbe, 0x176e0, 0x13270, 0x1993c,
          0x17670, 0x1bb3c, 0x1991e, 0x17638, 0x1321c, 0x1761c,
          0x1320e, 0x1760e, 0x11178, 0x188be, 0x13378, 0x1113c,
          0x17778, 0x1333c, 0x1111e, 0x1773c, 0x1331e, 0x1771e,
          0x111be, 0x133be, 0x177be, 0x172c0, 0x1b970, 0x1dcbc,
          0x17260, 0x1b938, 0x1dc9e, 0x17230, 0x1b91c, 0x17218,
          0x1b90e, 0x1720c, 0x17206, 0x13170, 0x198bc, 0x17370,
          0x13138, 0x1989e, 0x17338, 0x1b99e, 0x1731c, 0x1310e,
          0x1730e, 0x110bc, 0x131bc, 0x1109e, 0x173bc, 0x1319e,
          0x1739e, 0x17160, 0x1b8b8, 0x1dc5e, 0x17130, 0x1b89c,
          0x17118, 0x1b88e, 0x1710c, 0x17106, 0x130b8, 0x1985e,
          0x171b8, 0x1309c, 0x1719c, 0x1308e, 0x1718e, 0x1105e,
          0x130de, 0x171de, 0x170b0, 0x1b85c, 0x17098, 0x1b84e,
          0x1708c, 0x17086, 0x1305c, 0x170dc, 0x1304e, 0x170ce,
          0x17058, 0x1b82e, 0x1704c, 0x17046, 0x1302e, 0x1706e,
          0x1702c, 0x17026, 0x10af0, 0x1857c, 0x10a78, 0x1853e,
          0x10a3c, 0x10a1e, 0x10b7c, 0x10b3e, 0x1f0ba, 0x1e17a,
          0x1c2fa, 0x185fa, 0x11ae0, 0x18d78, 0x1c6be, 0x11a70,
          0x18d3c, 0x11a38, 0x18d1e, 0x11a1c, 0x11a0e, 0x10978,
          0x184be, 0x11b78, 0x1093c, 0x11b3c, 0x1091e, 0x11b1e,
          0x109be, 0x11bbe, 0x13ac0, 0x19d70, 0x1cebc, 0x13a60,
          0x19d38, 0x1ce9e, 0x13a30, 0x19d1c, 0x13a18, 0x19d0e,
          0x13a0c, 0x13a06, 0x11970, 0x18cbc, 0x13b70, 0x11938,
          0x18c9e, 0x13b38, 0x1191c, 0x13b1c, 0x1190e, 0x13b0e,
          0x108bc, 0x119bc, 0x1089e, 0x13bbc, 0x1199e, 0x13b9e,
          0x1bd60, 0x1deb8, 0x1ef5e, 0x17a40, 0x1bd30, 0x1de9c,
          0x17a20, 0x1bd18, 0x1de8e, 0x17a10, 0x1bd0c, 0x17a08,
          0x1bd06, 0x17a04, 0x13960, 0x19cb8, 0x1ce5e, 0x17b60,
          0x13930, 0x19c9c, 0x17b30, 0x1bd9c, 0x19c8e, 0x17b18,
          0x1390c, 0x17b0c, 0x13906, 0x17b06, 0x118b8, 0x18c5e,
          0x139b8, 0x1189c, 0x17bb8, 0x1399c, 0x1188e, 0x17b9c,
          0x1398e, 0x17b8e, 0x1085e, 0x118de, 0x139de, 0x17bde,
          0x17940, 0x1bcb0, 0x1de5c, 0x17920, 0x1bc98, 0x1de4e,
          0x17910, 0x1bc8c, 0x17908, 0x1bc86, 0x17904, 0x17902,
          0x138b0, 0x19c5c, 0x179b0, 0x13898, 0x19c4e, 0x17998,
          0x1bcce, 0x1798c, 0x13886, 0x17986, 0x1185c, 0x138dc,
          0x1184e, 0x179dc, 0x138ce, 0x179ce, 0x178a0, 0x1bc58,
          0x1de2e, 0x17890, 0x1bc4c, 0x17888, 0x1bc46, 0x17884,
          0x17882, 0x13858, 0x19c2e, 0x178d8, 0x1384c, 0x178cc,
          0x13846, 0x178c6, 0x1182e, 0x1386e, 0x178ee, 0x17850,
          0x1bc2c, 0x17848, 0x1bc26, 0x17844, 0x17842, 0x1382c,
          0x1786c, 0x13826, 0x17866, 0x17828, 0x1bc16, 0x17824,
          0x17822, 0x13816, 0x17836, 0x10578, 0x182be, 0x1053c,
          0x1051e, 0x105be, 0x10d70, 0x186bc, 0x10d38, 0x1869e,
          0x10d1c, 0x10d0e, 0x104bc, 0x10dbc, 0x1049e, 0x10d9e,
          0x11d60, 0x18eb8, 0x1c75e, 0x11d30, 0x18e9c, 0x11d18,
          0x18e8e, 0x11d0c, 0x11d06, 0x10cb8, 0x1865e, 0x11db8,
          0x10c9c, 0x11d9c, 0x10c8e, 0x11d8e, 0x1045e, 0x10cde,
          0x11dde, 0x13d40, 0x19eb0, 0x1cf5c, 0x13d20, 0x19e98,
          0x1cf4e, 0x13d10, 0x19e8c, 0x13d08, 0x19e86, 0x13d04,
          0x13d02, 0x11cb0, 0x18e5c, 0x13db0, 0x11c98, 0x18e4e,
          0x13d98, 0x19ece, 0x13d8c, 0x11c86, 0x13d86, 0x10c5c,
          0x11cdc, 0x10c4e, 0x13ddc, 0x11cce, 0x13dce, 0x1bea0,
          0x1df58, 0x1efae, 0x1be90, 0x1df4c, 0x1be88, 0x1df46,
          0x1be84, 0x1be82, 0x13ca0, 0x19e58, 0x1cf2e, 0x17da0,
          0x13c90, 0x19e4c, 0x17d90, 0x1becc, 0x19e46, 0x17d88,
          0x13c84, 0x17d84, 0x13c82, 0x17d82, 0x11c58, 0x18e2e,
          0x13cd8, 0x11c4c, 0x17dd8, 0x13ccc, 0x11c46, 0x17dcc,
          0x13cc6, 0x17dc6, 0x10c2e, 0x11c6e, 0x13cee, 0x17dee,
          0x1be50, 0x1df2c, 0x1be48, 0x1df26, 0x1be44, 0x1be42,
          0x13c50, 0x19e2c, 0x17cd0, 0x13c48, 0x19e26, 0x17cc8,
          0x1be66, 0x17cc4, 0x13c42, 0x17cc2, 0x11c2c, 0x13c6c,
          0x11c26, 0x17cec, 0x13c66, 0x17ce6, 0x1be28, 0x1df16,
          0x1be24, 0x1be22, 0x13c28, 0x19e16, 0x17c68, 0x13c24,
          0x17c64, 0x13c22, 0x17c62, 0x11c16, 0x13c36, 0x17c76,
          0x1be14, 0x1be12, 0x13c14, 0x17c34, 0x13c12, 0x17c32,
          0x102bc, 0x1029e, 0x106b8, 0x1835e, 0x1069c, 0x1068e,
          0x1025e, 0x106de, 0x10eb0, 0x1875c, 0x10e98, 0x1874e,
          0x10e8c, 0x10e86, 0x1065c, 0x10edc, 0x1064e, 0x10ece,
          0x11ea0, 0x18f58, 0x1c7ae, 0x11e90, 0x18f4c, 0x11e88,
          0x18f46, 0x11e84, 0x11e82, 0x10e58, 0x1872e, 0x11ed8,
          0x18f6e, 0x11ecc, 0x10e46, 0x11ec6, 0x1062e, 0x10e6e,
          0x11eee, 0x19f50, 0x1cfac, 0x19f48, 0x1cfa6, 0x19f44,
          0x19f42, 0x11e50, 0x18f2c, 0x13ed0, 0x19f6c, 0x18f26,
          0x13ec8, 0x11e44, 0x13ec4, 0x11e42, 0x13ec2, 0x10e2c,
          0x11e6c, 0x10e26, 0x13eec, 0x11e66, 0x13ee6, 0x1dfa8,
          0x1efd6, 0x1dfa4, 0x1dfa2, 0x19f28, 0x1cf96, 0x1bf68,
          0x19f24, 0x1bf64, 0x19f22, 0x1bf62, 0x11e28, 0x18f16,
          0x13e68, 0x11e24, 0x17ee8, 0x13e64, 0x11e22, 0x17ee4,
          0x13e62, 0x17ee2, 0x10e16, 0x11e36, 0x13e76, 0x17ef6,
          0x1df94, 0x1df92, 0x19f14, 0x1bf34, 0x19f12, 0x1bf32,
          0x11e14, 0x13e34, 0x11e12, 0x17e74, 0x13e32, 0x17e72,
          0x1df8a, 0x19f0a, 0x1bf1a, 0x11e0a, 0x13e1a, 0x17e3a,
          0x1035c, 0x1034e, 0x10758, 0x183ae, 0x1074c, 0x10746,
          0x1032e, 0x1076e, 0x10f50, 0x187ac, 0x10f48, 0x187a6,
          0x10f44, 0x10f42, 0x1072c, 0x10f6c, 0x10726, 0x10f66,
          0x18fa8, 0x1c7d6, 0x18fa4, 0x18fa2, 0x10f28, 0x18796,
          0x11f68, 0x18fb6, 0x11f64, 0x10f22, 0x11f62, 0x10716,
          0x10f36, 0x11f76, 0x1cfd4, 0x1cfd2, 0x18f94, 0x19fb4,
          0x18f92, 0x19fb2, 0x10f14, 0x11f34, 0x10f12, 0x13f74,
          0x11f32, 0x13f72, 0x1cfca, 0x18f8a, 0x19f9a, 0x10f0a,
          0x11f1a, 0x13f3a, 0x103ac, 0x103a6, 0x107a8, 0x183d6,
          0x107a4, 0x107a2, 0x10396, 0x107b6, 0x187d4, 0x187d2,
          0x10794, 0x10fb4, 0x10792, 0x10fb2, 0x1c7ea]];

window.TypstJS = (function () {
    const DPI = 72.0;
    const MAX_TABLE_COLS = 16;

    // Helvetica character width table (1/1000 em) matching typst.c
    const CHAR_W = {
        32:278,33:278,34:355,35:556,36:556,37:889,38:667,39:191,40:333,41:333,42:389,43:584,44:278,45:333,46:278,47:278,
        48:556,49:556,50:556,51:556,52:556,53:556,54:556,55:556,56:556,57:556,58:278,59:278,60:584,61:584,62:584,63:556,
        64:1015,65:667,66:667,67:722,68:722,69:667,70:611,71:778,72:722,73:278,74:500,75:667,76:556,77:833,78:722,79:778,
        80:667,81:778,82:722,83:667,84:611,85:722,86:667,87:944,88:667,89:667,90:611,91:278,92:278,93:278,94:469,95:556,
        96:222,97:556,98:556,99:500,100:556,101:556,102:278,103:556,104:556,105:222,106:222,107:500,108:222,109:833,110:556,
        111:556,112:556,113:556,114:333,115:500,116:278,117:556,118:500,119:722,120:500,121:500,122:500,123:333,124:260,
        125:333,126:584,0x95:350,0x99:600,150:500,151:1000,169:600,174:600,176:333,178:333,183:278
    };

    const UPC_L = ["0001101","0011001","0010011","0111101","0100011","0110001","0101111","0111011","0110111","0001011"];
    const UPC_R = ["1110010","1100110","1101100","1000010","1011100","1001110","1010000","1000100","1001000","1110100"];
    const C128_PAT = [
        "212222","222122","222221","121223","121322","131222","122213","122312","132212","221213","221312","231212","112232","122132","122231","113222","123122","123221","223211","221132",
        "221231","213212","223112","312131","311222","321122","321221","312212","322112","322211","212123","212321","232121","111323","131123","131321","112313","132113","132311","211313",
        "231113","231311","112133","112331","132131","113123","113321","133121","313121","211331","231131","213113","213311","213131","311123","311321","331121","312113","312311","332111",
        "314111","221411","431111","111224","111422","121124","121421","141122","141221","112214","112412","122114","122411","142112","142211","241211","221114","413111","241112","134111",
        "111242","121142","121241","114212","124112","124211","411212","421112","421211","212141","214121","412121","111143","111341","131141","114113","114311","411113","411311","113141",
        "114131","311141","411131","211412","211214","211232","2331112"
    ];

    const QR_TOTAL_CW = [26, 44, 70, 100, 134, 172, 196, 242, 292, 346];
    const QR_ECC_PB = [[7,10,13,17],[10,16,22,28],[15,26,18,22],[20,18,26,16],[26,24,18,22],[18,16,24,28],[20,18,18,26],[24,22,22,26],[30,22,20,24],[18,26,24,28]];
    const QR_BLOCKS = [[1,1,1,1],[1,1,1,1],[1,1,2,2],[1,2,2,4],[1,2,4,4],[2,4,4,4],[2,4,6,5],[2,4,6,6],[2,5,8,8],[4,5,8,8]];
    const DM_SIZES = [
        {w:10,data_cw:3,ecc_cw:5,regions:1,blocks:1},{w:12,data_cw:5,ecc_cw:7,regions:1,blocks:1},{w:14,data_cw:8,ecc_cw:10,regions:1,blocks:1},
        {w:16,data_cw:12,ecc_cw:12,regions:1,blocks:1},{w:18,data_cw:18,ecc_cw:14,regions:1,blocks:1},{w:20,data_cw:22,ecc_cw:18,regions:1,blocks:1},
        {w:22,data_cw:30,ecc_cw:20,regions:1,blocks:1},{w:24,data_cw:36,ecc_cw:24,regions:1,blocks:1},{w:26,data_cw:44,ecc_cw:28,regions:1,blocks:1},
        {w:32,data_cw:62,ecc_cw:36,regions:2,blocks:1},{w:36,data_cw:86,ecc_cw:42,regions:2,blocks:1},{w:40,data_cw:114,ecc_cw:48,regions:2,blocks:1},
        {w:44,data_cw:144,ecc_cw:56,regions:2,blocks:1},{w:48,data_cw:174,ecc_cw:68,regions:2,blocks:1},{w:52,data_cw:204,ecc_cw:84,regions:2,blocks:2}
    ];

    const qrExp = new Uint8Array(512), qrLog = new Uint8Array(256);
    const dmExp = new Uint8Array(512), dmLog = new Uint8Array(256);
    (function initGF() {
        let x = 1;
        for (let i = 0; i < 255; i++) { qrExp[i] = x; qrLog[x] = i; x <<= 1; if (x & 256) x ^= 0x11D; }
        for (let i = 255; i < 512; i++) qrExp[i] = qrExp[i - 255];
        x = 1;
        for (let i = 0; i < 255; i++) { dmExp[i] = x; dmLog[x] = i; x <<= 1; if (x & 256) x ^= 0x12D; }
        for (let i = 255; i < 512; i++) dmExp[i] = dmExp[i - 255];
    })();

    const qrMul = (a, b) => (!a || !b) ? 0 : qrExp[qrLog[a] + qrLog[b]];
    const dmMul = (a, b) => (!a || !b) ? 0 : dmExp[dmLog[a] + dmLog[b]];

    function generateQrMatrix(text) {
        const len = text.length, eccLevel = 1;
        let ver = -1;
        for (let v = 1; v <= 10; v++) {
            const dcw = QR_TOTAL_CW[v - 1] - QR_ECC_PB[v - 1][eccLevel] * QR_BLOCKS[v - 1][eccLevel];
            if (len <= dcw - (v <= 9 ? 2 : 3)) { ver = v; break; }
        }
        if (ver === -1) return null;
        const size = 17 + 4 * ver;
        const totalCw = QR_TOTAL_CW[ver - 1], eccPb = QR_ECC_PB[ver - 1][eccLevel], numBlocks = QR_BLOCKS[ver - 1][eccLevel];
        const totalDataCw = totalCw - eccPb * numBlocks;

        const dataCw = new Uint8Array(350);
        let bp = 0;
        const writeBit = (val) => { if (val) dataCw[bp >> 3] |= (1 << (7 - (bp & 7))); bp++; };
        writeBit(0); writeBit(1); writeBit(0); writeBit(0);
        for (let i = (ver <= 9 ? 7 : 15); i >= 0; i--) writeBit((len >> i) & 1);
        for (let i = 0; i < len; i++) { const c = text.charCodeAt(i) & 0xFF; for (let j = 7; j >= 0; j--) writeBit((c >> j) & 1); }
        for (let i = 0; i < 4 && bp < totalDataCw * 8; i++) writeBit(0);
        while (bp % 8 !== 0 && bp < totalDataCw * 8) writeBit(0);
        const pad = [0xEC, 0x11]; let pi = 0;
        while (bp < totalDataCw * 8) { for (let j = 7; j >= 0; j--) writeBit((pad[pi] >> j) & 1); pi = 1 - pi; }

        const rsGen = new Uint8Array(32); rsGen[0] = 1;
        for (let i = 0; i < eccPb; i++) for (let j = i + 1; j > 0; j--) rsGen[j] ^= qrMul(rsGen[j - 1], qrExp[i]);

        const g2Blocks = totalDataCw % numBlocks, g1Blocks = numBlocks - g2Blocks;
        const g1Len = Math.floor(totalDataCw / numBlocks), g2Len = g1Len + 1;
        let srcIdx = 0;
        const blkData = Array.from({length: numBlocks}, () => new Uint8Array(128));
        const blkEcc = Array.from({length: numBlocks}, () => new Uint8Array(32));

        for (let b = 0; b < numBlocks; b++) {
            const blen = b < g1Blocks ? g1Len : g2Len;
            for (let i = 0; i < blen; i++) blkData[b][i] = dataCw[srcIdx++];
            for (let i = 0; i < blen; i++) {
                const fb = blkData[b][i] ^ blkEcc[b][0];
                for (let j = 0; j < eccPb - 1; j++) blkEcc[b][j] = blkEcc[b][j + 1] ^ qrMul(fb, rsGen[j + 1]);
                blkEcc[b][eccPb - 1] = qrMul(fb, rsGen[eccPb]);
            }
        }

        const finalCw = new Uint8Array(400); let outIdx = 0;
        for (let col = 0; col < g1Len; col++) for (let b = 0; b < numBlocks; b++) finalCw[outIdx++] = blkData[b][col];
        if (g2Blocks > 0) for (let b = g1Blocks; b < numBlocks; b++) finalCw[outIdx++] = blkData[b][g1Len];
        for (let col = 0; col < eccPb; col++) for (let b = 0; b < numBlocks; b++) finalCw[outIdx++] = blkEcc[b][col];

        const sym = Array.from({length: size}, () => new Int8Array(size).fill(-1));
        for (let fy = 0; fy < 3; fy++) {
            const cy = fy === 2 ? size - 7 : 0, cx = fy === 1 ? size - 7 : 0;
            for (let dy = -1; dy <= 7; dy++) for (let dx = -1; dx <= 7; dx++) {
                const py = cy + dy, px = cx + dx;
                if (py >= 0 && py < size && px >= 0 && px < size)
                    sym[py][px] = (dy === -1 || dy === 7 || dx === -1 || dx === 7) ? 0 : (Math.max(Math.abs(dx - 3), Math.abs(dy - 3)) === 2 ? 0 : 1);
            }
        }

        const alignPos = [[0,0,0],[6,18,0],[6,22,0],[6,26,0],[6,30,0],[6,34,0],[6,22,38],[6,24,42],[6,26,46],[6,28,50]];
        if (ver > 1) {
            for (let i = 0; i < 3; i++) for (let j = 0; j < 3; j++) {
                const cy = alignPos[ver - 1][i], cx = alignPos[ver - 1][j];
                if (!cy || !cx || sym[cy][cx] !== -1) continue;
                for (let dy = -2; dy <= 2; dy++) for (let dx = -2; dx <= 2; dx++)
                    sym[cy + dy][cx + dx] = Math.max(Math.abs(dx), Math.abs(dy)) === 1 ? 0 : 1;
            }
        }
        for (let i = 8; i < size - 8; i++) {
            if (sym[6][i] === -1) sym[6][i] = (i % 2 === 0) ? 1 : 0;
            if (sym[i][6] === -1) sym[i][6] = (i % 2 === 0) ? 1 : 0;
        }
        sym[size - 8][8] = 1;

        const ftx1 = [0,1,2,3,4,5,7,8,8,8,8,8,8,8,8], fty1 = [8,8,8,8,8,8,8,8,7,5,4,3,2,1,0];
        for (let i = 0; i < 15; i++) {
            const b = (0x5412 >> (14 - i)) & 1;
            sym[fty1[i]][ftx1[i]] = b;
            if (i < 7) sym[size - 1 - i][8] = b; else sym[8][size - 15 + i] = b;
        }

        let x = size - 1, y = size - 1, dir = -1, bitIdx = 0;
        while (x > 0) {
            if (x === 6) x--;
            for (let i = 0; i < 2; i++) {
                const cx = x - i;
                if (sym[y][cx] === -1) {
                    let b = bitIdx < totalCw * 8 ? (finalCw[bitIdx >> 3] >> (7 - (bitIdx & 7))) & 1 : 0;
                    if ((cx + y) % 2 === 0) b ^= 1;
                    sym[y][cx] = b; bitIdx++;
                }
            }
            y += dir;
            if (y < 0 || y >= size) { dir = -dir; y += dir; x -= 2; }
        }
        return { sym, size };
    }

    function buildDataMatrix(text) {
        const len = text.length;
        const sizeIdx = DM_SIZES.findIndex(s => len <= s.data_cw);
        if (sizeIdx === -1) return null;
        const { w: targetW, data_cw: dataCw, ecc_cw: eccCw, regions, blocks } = DM_SIZES[sizeIdx];
        const nrow = targetW - 2 * regions, ncol = targetW - 2 * regions;

        const wd = new Uint8Array(512);
        for (let i = 0; i < len; i++) wd[i] = (text.charCodeAt(i) & 0xFF) + 1;
        if (len < dataCw) {
            wd[len] = 129;
            for (let i = len + 1; i < dataCw; i++) {
                const v = 129 + (((149 * (i + 1)) % 253) + 1);
                wd[i] = v <= 254 ? v : v - 254;
            }
        }
        const dPerBlk = dataCw / blocks, ePerBlk = eccCw / blocks;
        const g = new Uint8Array(128); g[0] = 1;
        for (let i = 1; i <= ePerBlk; i++) {
            g[i] = g[i - 1];
            for (let j = i - 1; j > 0; j--) g[j] = g[j - 1] ^ dmMul(g[j], dmExp[i]);
            g[0] = dmMul(g[0], dmExp[i]);
        }
        for (let b = 0; b < blocks; b++) {
            const ecc = new Uint8Array(128);
            for (let i = 0; i < dPerBlk; i++) {
                const fb = wd[i * blocks + b] ^ ecc[0];
                for (let j = 0; j < ePerBlk - 1; j++) ecc[j] = ecc[j + 1] ^ dmMul(fb, g[ePerBlk - 1 - j]);
                ecc[ePerBlk - 1] = dmMul(fb, g[0]);
            }
            for (let i = 0; i < ePerBlk; i++) wd[dataCw + i * blocks + b] = ecc[i];
        }

        const grid = new Int8Array(nrow * ncol).fill(-1);
        const mod = (r, c, val) => {
            if (r < 0) { r += nrow; c += 4 - ((nrow + 3) % 8); }
            if (c < 0) { c += ncol; r += 4 - ((ncol + 3) % 8); }
            grid[r * ncol + c] = val;
        };
        const utah = (r, c, v) => {
            mod(r-2,c-2,(v>>7)&1); mod(r-2,c-1,(v>>6)&1); mod(r-1,c-2,(v>>5)&1); mod(r-1,c-1,(v>>4)&1);
            mod(r-1,c,(v>>3)&1); mod(r,c-2,(v>>2)&1); mod(r,c-1,(v>>1)&1); mod(r,c,v&1);
        };
        let r = 4, c = 0, i = 0;
        while (i < dataCw + eccCw) {
            if (r === nrow && c === 0) {
                const v = wd[i++];
                mod(nrow-1,0,(v>>7)&1); mod(nrow-1,1,(v>>6)&1); mod(nrow-1,2,(v>>5)&1); mod(0,ncol-2,(v>>4)&1);
                mod(0,ncol-1,(v>>3)&1); mod(1,ncol-1,(v>>2)&1); mod(2,ncol-1,(v>>1)&1); mod(3,ncol-1,v&1);
                r -= 2; c += 2; continue;
            }
            if (r === nrow - 2 && c === 0 && (ncol % 4) !== 0) {
                const v = wd[i++];
                mod(nrow-3,0,(v>>7)&1); mod(nrow-2,0,(v>>6)&1); mod(nrow-1,0,(v>>5)&1); mod(0,ncol-4,(v>>4)&1);
                mod(0,ncol-3,(v>>3)&1); mod(0,ncol-2,(v>>2)&1); mod(0,ncol-1,(v>>1)&1); mod(1,ncol-1,v&1);
                r -= 2; c += 2; continue;
            }
            if (r === nrow - 2 && c === 0 && (ncol % 8) === 4) {
                const v = wd[i++];
                mod(nrow-3,0,(v>>7)&1); mod(nrow-2,0,(v>>6)&1); mod(nrow-1,0,(v>>5)&1); mod(0,ncol-2,(v>>4)&1);
                mod(0,ncol-1,(v>>3)&1); mod(1,ncol-1,(v>>2)&1); mod(2,ncol-1,(v>>1)&1); mod(3,ncol-1,v&1);
                r -= 2; c += 2; continue;
            }
            if (r === nrow + 4 && c === 2 && (ncol % 8) === 0) {
                const v = wd[i++];
                mod(nrow-1,0,(v>>7)&1); mod(nrow-1,ncol-1,(v>>6)&1); mod(0,ncol-3,(v>>5)&1); mod(0,ncol-2,(v>>4)&1);
                mod(0,ncol-1,(v>>3)&1); mod(1,ncol-3,(v>>2)&1); mod(1,ncol-2,(v>>1)&1); mod(1,ncol-1,v&1);
                r -= 2; c += 2; continue;
            }
            do { if (r >= 0 && r < nrow && c >= 0 && c < ncol && grid[r * ncol + c] === -1) utah(r, c, wd[i++]); r -= 2; c += 2; } while (r >= 0 && c < ncol);
            r++; c += 3;
            do { if (r >= 0 && r < nrow && c >= 0 && c < ncol && grid[r * ncol + c] === -1) utah(r, c, wd[i++]); r += 2; c -= 2; } while (r < nrow && c >= 0);
            r += 3; c++;
        }
        if (grid[nrow * ncol - 1] === -1) { grid[nrow * ncol - 1] = 1; grid[nrow * ncol - ncol - 2] = 0; }
        const regW = targetW / regions, regData = regW - 2;
        const bmp = Array.from({length: targetW}, () => new Uint8Array(targetW));
        for (let y = 0; y < targetW; y++) for (let x = 0; x < targetW; x++) {
            const rx = Math.floor(x / regW), ry = Math.floor(y / regW), lx = x % regW, ly = y % regW;
            let val;
            if (ly === regW - 1 || lx === 0) val = 1;
            else if (ly === 0) val = (lx % 2 === 0) ? 1 : 0;
            else if (lx === regW - 1) val = (ly % 2 === 1) ? 1 : 0;
            else val = grid[(ry * regData + (ly - 1)) * ncol + (rx * regData + (lx - 1))];
            bmp[y][x] = val === 1 ? 1 : 0;
        }
        return { bmp, size: targetW };
    }

function buildPdf417(text) {
        if (typeof PDF417_CLUSTERS === 'undefined' || !PDF417_CLUSTERS[0]?.length) return null;
        const cols = 5, eccLevel = 1;
        const cwBuf = [];
        const dLen = text.length;
        let dIdx = 0;

        cwBuf.push((dLen % 6 === 0) ? 924 : 901);
        while (dIdx + 5 < dLen) {
            let val = 0n;
            for (let j = 0; j < 6; j++) val = (val << 8n) | BigInt(text.charCodeAt(dIdx + j) & 0xFF);
            const cw = new Array(5);
            for (let j = 4; j >= 0; j--) { cw[j] = Number(val % 900n); val /= 900n; }
            for (let j = 0; j < 5; j++) cwBuf.push(cw[j]);
            dIdx += 6;
        }
        while (dIdx < dLen) cwBuf.push(text.charCodeAt(dIdx++) & 0xFF);

        const eccCount = 1 << (eccLevel + 1);
        const totalWords = cwBuf.length + 1 + eccCount;
        let rows = Math.ceil(totalWords / cols);
        if (rows < 3) rows = 3;
        if (rows > 90) return null;

        const padCount = (rows * cols) - totalWords;
        const payloadLen = cwBuf.length + 1 + padCount;
        if (payloadLen + eccCount > 928) return null;

        const fullBuf = new Int32Array(payloadLen + eccCount);
        fullBuf[0] = payloadLen;
        for (let i = 0; i < cwBuf.length; i++) fullBuf[1 + i] = cwBuf[i];
        for (let i = 0; i < padCount; i++) fullBuf[1 + cwBuf.length + i] = 900;

        const b = new Int32Array(eccCount + 1);
        b[0] = 1;
        let root = 1;
        for (let i = 1; i <= eccCount; i++) {
            root = (root * 3) % 929;
            for (let j = i; j > 0; j--) b[j] = (b[j] + (b[j - 1] * (929 - root))) % 929;
        }
        const eccGen = b.slice(1, eccCount + 1);

        const outEcc = new Int32Array(eccCount);
        for (let i = 0; i < payloadLen; i++) {
            const term = (fullBuf[i] + outEcc[0]) % 929;
            for (let j = 0; j < eccCount - 1; j++) {
                outEcc[j] = (outEcc[j + 1] + 929 - (term * eccGen[j]) % 929) % 929;
            }
            outEcc[eccCount - 1] = (929 - (term * eccGen[eccCount - 1]) % 929) % 929;
        }
        for (let i = 0; i < eccCount; i++) fullBuf[payloadLen + i] = outEcc[i] !== 0 ? 929 - outEcc[i] : 0;

        return { fullBuf, rows, cols, eccLevel, modulesPerRow: 17 + 17 + (cols * 17) + 17 + 18 };
    }

    /* ========================================================================
     * CORE ENGINE INSTANCE (Stateless per compile call)
     * ======================================================================== */
    function compileDocument(typstSource, opts = {}) {
        const colors = new Map();
        const addColor = (hex, name) => {
            const m = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})/i.exec(hex);
            colors.set(name, m ? { r: parseInt(m[1], 16), g: parseInt(m[2], 16), b: parseInt(m[3], 16), name } : { r: 0, g: 0, b: 0, name });
        };
        addColor('#2B6CB0', 'blue_primary'); addColor('#2D3748', 'gray_800'); addColor('#4A5568', 'gray_700');
        addColor('#718096', 'gray_600');     addColor('#A0AEC0', 'gray_500'); addColor('#CBD5E0', 'gray_400');
        addColor('#E2E8F0', 'gray_300');     addColor('#EDF2F7', 'gray_200'); addColor('#F7FAFC', 'gray_100');
        addColor('#FEFCBF', 'yellow_light'); addColor('#D69E2E', 'yellow_border');
        addColor('#FFFFFF', 'white');        addColor('#000000', 'black');

        function getColor(spec) {
            const fb = { r: 45, g: 55, b: 72, name: 'gray_800' };
            if (!spec) return fb;
            let s = String(spec).trim().replace(/^[+\s"']+/, '').replace(/["']+$/, '');
            if (!s) return fb;
            if (s === 'none' || s === 'transparent') return { r: 255, g: 255, b: 255, name: 'none' };
            if (s.startsWith('luma(')) { const v = parseInt(s.slice(5), 10) || 200; return { r: v, g: v, b: v, name: 'luma' }; }
            if (s.startsWith('rgb(')) {
                const inner = s.slice(4).replace(/\)$/, '').trim().replace(/^["']|["']$/g, '');
                if (inner.startsWith('#')) return getColor(inner);
                const p = inner.split(',').map(x => parseInt(x.trim(), 10));
                if (p.length >= 3 && !p.some(isNaN)) return { r: p[0], g: p[1], b: p[2], name: 'rgb_num' };
            }
            if (s === 'white') return { r: 255, g: 255, b: 255, name: 'white' };
            if (s === 'black') return { r: 0, g: 0, b: 0, name: 'black' };
            if (s.startsWith('#')) {
                const m = /^#([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})/i.exec(s);
                if (m) return { r: parseInt(m[1], 16), g: parseInt(m[2], 16), b: parseInt(m[3], 16), name: 'hex' };
            }
            return colors.get(s) || fb;
        }

        let pageWidth = opts.pageWidth || 612.0;
        let pageHeight = opts.pageHeight || 792.0;
        let marginTop = 70.86, marginBottom = 70.86, marginLeft = 56.69, marginRight = 56.69;
        let headerScript = '', footerScript = '';
        let currentState = { fontSize: 10.0, fillColor: getColor('#2D3748'), isBold: false, isItalic: false };
        const letDefs = new Map();

        let source = String(typstSource || '');
        let pos = 0;

        function parseSize(s) {
            if (!s) return 0;
            const v = parseFloat(s) || 0;
            if (s.includes('pt')) return v;
            if (s.includes('em')) return v * currentState.fontSize;
            if (s.includes('cm')) return v * DPI / 2.54;
            if (s.includes('mm')) return v * DPI / 25.4;
            if (s.includes('in')) return v * DPI;
            return v;
        }

        function sanitizeText(str) {
            return String(str || '')
                .replace(/\u2013/g, '\x96').replace(/\u2014/g, '\x97').replace(/\u2122/g, '\x99')
                .replace(/\u2022/g, '\x95').replace(/\u00B7/g, '\xB7').replace(/\u00B2/g, '\xB2')
                .replace(/\u00B0/g, '\xB0').replace(/\u00A9/g, '\xA9').replace(/\u00AE/g, '\xAE');
        }

        function skipWS() {
            while (pos < source.length) {
                const c = source[pos];
                if (c === ' ' || c === '\t' || c === '\r' || c === '\n') { pos++; continue; }
                if (c === '/' && source[pos + 1] === '/') { while (pos < source.length && source[pos] !== '\n') pos++; continue; }
                if (c === '/' && source[pos + 1] === '*') {
                    pos += 2;
                    while (pos < source.length && !(source[pos] === '*' && source[pos + 1] === '/')) pos++;
                    pos += 2; continue;
                }
                return source[pos];
            }
            return '';
        }

        function matchStr(s) {
            const saved = pos;
            skipWS();
            if (source.startsWith(s, pos)) { pos += s.length; return true; }
            pos = saved;
            return false;
        }

        function extractParamValue(params, key) {
            let i = 0;
            while (i < params.length) {
                while (i < params.length && /\s/.test(params[i])) i++;
                if (params.startsWith(key, i)) {
                    let j = i + key.length;
                    while (j < params.length && /\s/.test(params[j])) j++;
                    if (params[j] === ':') {
                        j++;
                        while (j < params.length && /\s/.test(params[j])) j++;
                        const start = j;
                        let pD = 0, bD = 0, brD = 0;
                        const sB = params[j] === '[', sBr = params[j] === '{';
                        while (j < params.length) {
                            const ch = params[j];
                            if (ch === '(') pD++; else if (ch === ')') pD--;
                            else if (ch === '[') bD++; else if (ch === ']') bD--;
                            else if (ch === '{') brD++; else if (ch === '}') brD--;
                            j++;
                            if (sB && bD === 0 && pD === 0 && brD === 0) break;
                            if (sBr && bD === 0 && pD === 0 && brD === 0) break;
                            if (!sB && !sBr && pD === 0 && bD === 0 && brD === 0 && params[j] === ',') break;
                        }
                        return params.slice(start, j).trim();
                    }
                }
                let pD = 0, bD = 0, brD = 0;
                while (i < params.length && (pD > 0 || bD > 0 || brD > 0 || params[i] !== ',')) {
                    if (params[i] === '(') pD++; else if (params[i] === ')') pD--;
                    else if (params[i] === '[') bD++; else if (params[i] === ']') bD--;
                    else if (params[i] === '{') brD++; else if (params[i] === '}') brD--;
                    i++;
                }
                if (params[i] === ',') i++;
            }
            return null;
        }

        function unwrapMarkup(str) {
            let s = String(str || '').trim();
            let changed = true;
            while (changed) {
                changed = false;
                s = s.trim();
                if (s.startsWith('[') && s.endsWith(']')) { s = s.slice(1, -1).trim(); changed = true; continue; }
                if (s.startsWith('{') && s.endsWith('}')) { s = s.slice(1, -1).trim(); changed = true; continue; }
                if (/^context(\s|$|\{|\[)/.test(s)) { s = s.slice(7).trim(); changed = true; continue; }
                const m = /^#?(?:text|align)\s*\(/.exec(s);
                if (m) {
                    let k = m[0].length, d = 1;
                    while (k < s.length && d > 0) { if (s[k] === '(') d++; else if (s[k] === ')') d--; k++; }
                    s = s.slice(k).trim(); changed = true; continue;
                }
            }
            if ((s.startsWith('"') && s.endsWith('"')) || (s.startsWith("'") && s.endsWith("'"))) s = s.slice(1, -1);
            return s;
        }

        function makeNode(type) {
            return {
                type, headingLevel: 0, content: '', width: 0, height: 0, hasStroke: false, hasWidth: 0, hasHeight: 0,
                align: 0, strokeColor: { r: 0, g: 0, b: 0, name: 'black' }, fillColor: { ...currentState.fillColor },
                strokeWidth: 0.5, fontSize: currentState.fontSize || 10, isBold: false, isItalic: false, radius: 0,
                altRows: false, headerFill: getColor('#2B6CB0'), stripe1: getColor('#FFFFFF'), stripe2: getColor('#F7FAFC'),
                cells: [], cellRows: 0, cellCols: 0, colWidths: [], colAligns: [], isFr: [], gutter: 0, inset: 0,
                chartTitle: '', chartLabels: [], chartScores: [], chartColor: '#2563eb', chartXLabel: '', chartYLabel: '', chartTrend: false,
                barcodeType: '', barcodeData: '', children: []
            };
        }

        function readParenBlock() {
            if (source[pos] !== '(') return '';
            pos++;
            let out = '', d = 1;
            while (pos < source.length && d > 0) {
                if (source[pos] === '(') d++; else if (source[pos] === ')') d--;
                if (d > 0) out += source[pos];
                pos++;
            }
            return out;
        }

        function parseTypstString(str) {
            if (!str) return null;
            const savedSrc = source, savedPos = pos;
            source = str; pos = 0;
            const blk = makeNode('BLOCK');
            while (skipWS()) {
                const prev = pos;
                const el = parseElement();
                if (el) blk.children.push(el);
                if (pos === prev) pos++;
            }
            source = savedSrc; pos = savedPos;
            return blk;
        }

        function parseSet() {
            if (matchStr('#set page(')) {
                pos--;
                const params = readParenBlock();
                const paper = extractParamValue(params, 'paper');
                if (paper && !opts.overridePageSize) {
                    if (paper.includes('us-letter') || paper.includes('letter')) { pageWidth = 612; pageHeight = 792; }
                    else if (paper.includes('a4')) { pageWidth = 595.28; pageHeight = 841.89; }
                }
                const margin = extractParamValue(params, 'margin');
                if (margin) {
                    const inner = margin.startsWith('(') ? margin.slice(1, margin.lastIndexOf(')')) : margin;
                    const mx = extractParamValue(inner, 'x'); if (mx) { marginLeft = marginRight = parseSize(mx); }
                    const my = extractParamValue(inner, 'y'); if (my) { marginTop = marginBottom = parseSize(my); }
                    const ml = extractParamValue(inner, 'left'); if (ml) marginLeft = parseSize(ml);
                    const mr = extractParamValue(inner, 'right'); if (mr) marginRight = parseSize(mr);
                    const mt = extractParamValue(inner, 'top'); if (mt) marginTop = parseSize(mt);
                    const mb = extractParamValue(inner, 'bottom'); if (mb) marginBottom = parseSize(mb);
                }
                const hdr = extractParamValue(params, 'header');
                if (hdr) headerScript = unwrapMarkup(hdr).replace(/#?counter\(page\)\.display\(\)/g, '%d');
                const ftr = extractParamValue(params, 'footer');
                if (ftr) footerScript = unwrapMarkup(ftr).replace(/#?counter\(page\)\.display\(\)/g, '%d');
                return;
            }
            if (matchStr('#set text(')) {
                pos--;
                const params = readParenBlock();
                const fill = extractParamValue(params, 'fill'); if (fill) currentState.fillColor = getColor(fill);
                const size = extractParamValue(params, 'size');
                if (size) currentState.fontSize = parseSize(size);
                else {
                    const m = /([\d.]+)\s*(?:pt|em)/.exec(params);
                    if (m) currentState.fontSize = parseSize(m[0]);
                }
                return;
            }
            while (pos < source.length && source[pos] !== '\n') pos++;
        }

        function parseLet() {
            matchStr('#let'); skipWS();
            let name = '';
            while (pos < source.length && /[\w-]/.test(source[pos])) name += source[pos++];
            if (!name) return;
            skipWS();
            let isFunc = false, paramName = '';
            if (source[pos] === '(') {
                isFunc = true; pos++;
                while (pos < source.length && source[pos] !== ')') paramName += source[pos++];
                if (source[pos] === ')') pos++;
            }
            skipWS(); if (source[pos] === '=') pos++; skipWS();
            if (source.startsWith('rgb(', pos)) {
                let v = ''; while (pos < source.length && source[pos] !== '\n') v += source[pos++];
                const c = getColor(v); c.name = name; colors.set(name, c); return;
            }
            let body = '';
            if (source[pos] === '[') {
                pos++; let d = 1;
                while (pos < source.length && d > 0) {
                    if (source[pos] === '[') d++; else if (source[pos] === ']') d--;
                    if (d > 0) body += source[pos];
                    pos++;
                }
            } else if (source[pos] === '{') {
                pos++; let d = 1;
                while (pos < source.length && d > 0) { if (source[pos] === '{') d++; else if (source[pos] === '}') d--; pos++; }
            } else {
                while (pos < source.length && source[pos] !== '\n') body += source[pos++];
            }
            letDefs.set(name, { name, isFunc, paramName: paramName.trim(), body });
        }

        function parseStrokeSpec(str, node) {
            node.hasStroke = true;
            const plus = str.indexOf('+');
            if (plus >= 0) {
                node.strokeWidth = parseFloat(str.slice(0, plus)) || 0.5;
                node.strokeColor = getColor(str.slice(plus + 1));
            } else {
                node.strokeWidth = 0.5;
                node.strokeColor = getColor(str);
            }
        }

        function parseBlockNode(type) {
            const b = makeNode(type);
            b.fillColor = { r: 255, g: 255, b: 255, name: 'none' };
            matchStr(source.startsWith('#', pos) ? source.slice(pos, source.indexOf('(', pos) > 0 ? source.indexOf('(', pos) : pos + 6) : '');
            const kw = /^#?(?:rect|box|block)/.exec(source.slice(pos));
            if (kw) pos += kw[0].length;
            skipWS();
            const params = source[pos] === '(' ? readParenBlock() : '';
            if (params) {
                const fill = extractParamValue(params, 'fill'); if (fill) b.fillColor = getColor(fill);
                const stroke = extractParamValue(params, 'stroke'); if (stroke) parseStrokeSpec(stroke, b);
                const inset = extractParamValue(params, 'inset'); if (inset) b.inset = parseSize(inset);
                const radius = extractParamValue(params, 'radius'); if (radius) b.radius = parseSize(radius);
                const width = extractParamValue(params, 'width');
                if (width) {
                    if (width.includes('%')) { b.width = parseFloat(width) / 100; b.hasWidth = 2; }
                    else { b.width = parseSize(width); b.hasWidth = 1; }
                }
            }
            skipWS();
            if (source[pos] === '[') {
                pos++; let d = 1, inner = '';
                while (pos < source.length && d > 0) {
                    if (source[pos] === '[') d++; else if (source[pos] === ']') d--;
                    if (d > 0) inner += source[pos];
                    pos++;
                }
                const sub = parseTypstString(inner);
                if (sub) b.children = sub.children;
            }
            return b;
        }

        function parseTableOrGrid(type) {
            const t = makeNode(type);
            const kw = /^#?(?:table|grid)/.exec(source.slice(pos));
            if (kw) pos += kw[0].length;
            skipWS();
            if (source[pos] !== '(') return t;
            pos++;
            let params = '', d = 1;
            while (pos < source.length && d > 0) {
                if (d === 1 && (source[pos] === '[' || /^#?(?:rect|box|block|align|line|table|grid)\(/.test(source.slice(pos)))) break;
                if (source[pos] === '(') d++; else if (source[pos] === ')') d--;
                if (d > 0) params += source[pos];
                pos++;
            }
            const cols = extractParamValue(params, 'columns');
            if (cols) {
                const parts = cols.replace(/^\(|\)$/g, '').split(',').map(s => s.trim()).filter(Boolean);
                parts.forEach(p => {
                    if (p.endsWith('fr')) { t.colWidths.push(parseFloat(p) || 1); t.isFr.push(true); }
                    else { t.colWidths.push(parseSize(p) || 100); t.isFr.push(false); }
                });
                t.cellCols = t.colWidths.length || 1;
            } else { t.cellCols = 1; t.colWidths = [1]; t.isFr = [true]; }

            const al = extractParamValue(params, 'align');
            t.colAligns = new Array(MAX_TABLE_COLS).fill(0);
            if (al) {
                if (al.startsWith('(')) {
                    al.slice(1, -1).split(',').forEach((a, i) => {
                        t.colAligns[i] = a.includes('center') ? 1 : a.includes('right') ? 2 : 0;
                    });
                } else {
                    const defA = al.includes('center') ? 1 : al.includes('right') ? 2 : 0;
                    t.colAligns.fill(defA);
                }
            }
            const stroke = extractParamValue(params, 'stroke'); if (stroke) parseStrokeSpec(stroke, t);
            const fill = extractParamValue(params, 'fill');
            if (fill) {
                if (fill.includes('calc.even') || fill.includes('row') || fill.includes('col-accent')) {
                    t.altRows = true; t.headerFill = getColor('col-accent');
                } else t.fillColor = getColor(fill);
            }
            const inset = extractParamValue(params, 'inset'); if (inset) t.inset = parseSize(inset);
            const gutter = extractParamValue(params, 'gutter'); if (gutter) t.gutter = parseSize(gutter);

            let cellIdx = 0;
            while (pos < source.length) {
                skipWS();
                if (source[pos] === ')' || source[pos] === ']') { if (source[pos] === ')') pos++; break; }
                const r = Math.floor(cellIdx / t.cellCols), c = cellIdx % t.cellCols;
                if (!t.cells[r]) t.cells[r] = [];
                const cell = makeNode('BLOCK');
                cell.align = t.colAligns[c] || 0;

                if (/^#?align\(/.test(source.slice(pos))) {
                    const m = /^#?align\(([^)]*)\)/.exec(source.slice(pos));
                    if (m) {
                        if (m[1].includes('right')) cell.align = 2;
                        else if (m[1].includes('center')) cell.align = 1;
                        pos += m[0].length;
                        skipWS();
                    }
                }
                if (source[pos] === '[') {
                    pos++; let bd = 1, inner = '';
                    while (pos < source.length && bd > 0) {
                        if (source[pos] === '[') bd++; else if (source[pos] === ']') bd--;
                        if (bd > 0) inner += source[pos];
                        pos++;
                    }
                    const sub = parseTypstString(inner);
                    if (sub) { sub.children.forEach(ch => { ch.align = cell.align; }); cell.children = sub.children; }
                } else {
                    const el = parseElement();
                    if (el) { el.align = cell.align; cell.children.push(el); }
                }
                t.cells[r][c] = cell;
                cellIdx++;
                skipWS();
                if (source[pos] === ',') pos++;
            }
            t.cellRows = Math.ceil(cellIdx / t.cellCols);
            return t;
        }

        function parseChartPairs(params, node) {
            const re = /\(\s*"([^"]+)"\s*,\s*([-+]?[\d.]+)\s*\)/g;
            let m;
            while ((m = re.exec(params)) && node.chartLabels.length < 8) {
                node.chartLabels.push(m[1]);
                node.chartScores.push(parseFloat(m[2]) || 0);
            }
        }

        function parseElement() {
            skipWS();
            if (pos >= source.length) return null;

            if (source[pos] === '=') {
                const h = makeNode('HEADING');
                while (source[pos] === '=') { h.headingLevel++; pos++; }
                skipWS();
                const start = pos;
                while (pos < source.length && source[pos] !== '\n') pos++;
                h.content = source.slice(start, pos).trim();
                h.fontSize = 18.0 - (h.headingLevel - 1) * 3.0;
                h.isBold = true;
                return h;
            }
            if (source.startsWith('#set', pos)) { parseSet(); return null; }
            if (source.startsWith('#let', pos)) { parseLet(); return null; }
            if (source.startsWith('#pagebreak', pos)) {
                pos += 10; skipWS(); if (source[pos] === '(') readParenBlock();
                return makeNode('PAGEBREAK');
            }
            if (source.startsWith('#show', pos) || source.startsWith('#import', pos)) {
                while (pos < source.length && source[pos] !== '\n') pos++;
                return null;
            }
            if (source.startsWith('#v(', pos)) {
                pos += 2; const p = readParenBlock();
                const v = makeNode('VSPACE'); v.height = parseSize(p); return v;
            }
            if (source[pos] === '-' && (source[pos + 1] === ' ' || source[pos + 1] === '\t')) {
                pos += 2;
                const start = pos; while (pos < source.length && source[pos] !== '\n') pos++;
                const p = makeNode('PARAGRAPH');
                p.content = '*\x95* ' + source.slice(start, pos).trim();
                return p;
            }

            if (source[pos] === '#') {
                const nm = /^#([\w-]+)/.exec(source.slice(pos));
                const name = nm ? nm[1] : '';
                if (name === 'report-header' && letDefs.has(name)) {
                    pos += nm[0].length; skipWS();
                    let sub = 'Comprehensive DXA Report';
                    if (source[pos] === '(') sub = unwrapMarkup(readParenBlock());
                    return parseTypstString(letDefs.get(name).body.replace(/#subtitle/g, sub));
                }
                if (name === 'patient-info-block' && letDefs.has(name)) {
                    pos += nm[0].length;
                    return parseTypstString(letDefs.get(name).body);
                }
                if (name === 'pie-chart' || name === 'line-chart' || name === 'bar-chart') {
                    pos += nm[0].length; skipWS();
                    const params = readParenBlock();
                    const c = makeNode(name === 'pie-chart' ? 'PIE_CHART' : name === 'line-chart' ? 'LINE_CHART' : 'BAR_CHART');
                    c.chartTitle = unwrapMarkup(extractParamValue(params, 'title') || '');
                    c.chartXLabel = unwrapMarkup(extractParamValue(params, 'x-label') || '').replace(/["'*_]/g, '');
                    c.chartYLabel = unwrapMarkup(extractParamValue(params, 'y-label') || '').replace(/["'*_]/g, '');
                    const col = extractParamValue(params, name === 'bar-chart' ? 'fill' : 'stroke') || extractParamValue(params, 'color');
                    if (col) {
                        let cv = unwrapMarkup(col);
                        if (cv.includes('+')) cv = cv.split('+').pop().trim();
                        c.chartColor = cv || '#2563eb';
                    } else if (params.includes('random')) c.chartColor = 'random';
                    const tl = extractParamValue(params, 'trend-line');
                    if (tl && tl.includes('true')) c.chartTrend = true;
                    parseChartPairs(params, c);
                    return c;
                }
                if (name === 'tscore-bar-chart' || name === 'bmd-trend-chart') {
                    pos += nm[0].length; skipWS();
                    const params = readParenBlock();
                    const c = makeNode(name === 'tscore-bar-chart' ? 'TSCORE_CHART' : 'BMD_CHART');
                    c.chartTitle = unwrapMarkup(extractParamValue(params, 'title') || (name === 'tscore-bar-chart' ? 'Vertebral Level T-Score Profile' : 'L1-L4 Spine BMD Trend vs Age Norms'));
                    const re = /label:\s*["']?([^"',)]+)["']?[^)]*score:\s*([-+]?[\d.]+)/g;
                    let m;
                    while ((m = re.exec(params)) && c.chartLabels.length < 8) {
                        c.chartLabels.push(m[1].trim()); c.chartScores.push(parseFloat(m[2]) || 0);
                    }
                    if (!c.chartLabels.length) { c.chartLabels = ['L1','L2','L3','L4','L1-L4']; c.chartScores = [-2.8,-2.6,-2.5,-2.4,-2.6]; }
                    return c;
                }
                if (name === 'barcode') {
                    pos += nm[0].length; skipWS();
                    const params = readParenBlock();
                    const bc = makeNode('BARCODE');
                    bc.barcodeType = unwrapMarkup(extractParamValue(params, 'type') || '').toLowerCase();
                    bc.barcodeData = unwrapMarkup(extractParamValue(params, 'data') || '');
                    const w = extractParamValue(params, 'width');
                    if (w) { if (w.includes('%')) { bc.width = parseFloat(w) / 100; bc.hasWidth = 2; } else { bc.width = parseSize(w); bc.hasWidth = 1; } }
                    const h = extractParamValue(params, 'height');
                    if (h) { if (h.includes('%')) { bc.height = parseFloat(h) / 100; bc.hasHeight = 2; } else { bc.height = parseSize(h); bc.hasHeight = 1; } }
                    else { bc.height = 50; bc.hasHeight = 1; }
                    return bc;
                }
            }

            if (/^#?line\(/.test(source.slice(pos))) {
                const l = makeNode('LINE');
                pos += source[pos] === '#' ? 5 : 4;
                const params = readParenBlock();
                const st = extractParamValue(params, 'stroke'); if (st) parseStrokeSpec(st, l);
                const len = extractParamValue(params, 'length');
                if (len) { if (len.includes('%')) { l.width = parseFloat(len) / 100; l.hasWidth = 2; } else { l.width = parseSize(len); l.hasWidth = 1; } }
                return l;
            }
            if (/^#?(?:rect|box)\(/.test(source.slice(pos))) return parseBlockNode('RECT');
            if (/^#?block\(/.test(source.slice(pos))) return parseBlockNode('BLOCK');
            if (/^#?grid\(/.test(source.slice(pos))) return parseTableOrGrid('GRID');
            if (/^#?table\(/.test(source.slice(pos))) return parseTableOrGrid('TABLE');
            if (/^#?(?:align|pad)\(/.test(source.slice(pos))) {
                const b = makeNode('BLOCK');
                const m = /^#?(?:align|pad)\(([^)]*)\)/.exec(source.slice(pos));
                if (m) {
                    if (m[1].includes('center')) b.align = 1;
                    else if (m[1].includes('right')) b.align = 2;
                    pos += m[0].length;
                }
                skipWS();
                if (source[pos] === '[') {
                    pos++; let d = 1, inner = '';
                    while (pos < source.length && d > 0) { if (source[pos] === '[') d++; else if (source[pos] === ']') d--; if (d > 0) inner += source[pos]; pos++; }
                    const sub = parseTypstString(inner);
                    if (sub) { sub.children.forEach(c => { c.align = b.align; }); b.children = sub.children; }
                }
                return b;
            }

            const start = pos;
            while (pos < source.length && source[pos] !== '\n') pos++;
            const txt = source.slice(start, pos).trim();
            if (!txt) return null;
            const p = makeNode('PARAGRAPH');
            p.content = txt;
            return p;
        }

        function parseInlineRuns(input, baseState) {
            const s = sanitizeText(input);
            const runs = [];
            let state = { ...baseState };
            let buf = '', i = 0;
            const flush = () => { if (buf) { runs.push({ text: buf, ...state }); buf = ''; } };

            while (i < s.length) {
                if (s.startsWith('(R)', i) || s.startsWith('(r)', i)) { buf += '\xAE'; i += 3; continue; }
                if (s.startsWith('(C)', i) || s.startsWith('(c)', i)) { buf += '\xA9'; i += 3; continue; }
                if (s.startsWith('(TM)', i) || s.startsWith('(tm)', i)) { buf += '\x99'; i += 4; continue; }
                if (s.startsWith('---', i)) { buf += '\x97'; i += 3; continue; }
                if (s.startsWith('--', i)) { buf += '\x96'; i += 2; continue; }
                if (s[i] === '\\' && (i + 1 >= s.length || s[i + 1] === ' ' || s[i + 1] === '\n')) {
                    flush(); runs.push({ text: '\n', ...state }); i += (s[i + 1] === '\n' ? 2 : 1); continue;
                }
                if (s[i] === '\\' && i + 1 < s.length && !/[a-zA-Z0-9\s]/.test(s[i + 1])) { buf += s[i + 1]; i += 2; continue; }
                if (s[i] === '*') { flush(); state.isBold = !state.isBold; i++; continue; }
                if (s.startsWith('#text(', i) || s.startsWith('text(', i)) {
                    flush();
                    i += s[i] === '#' ? 6 : 5;
                    let params = '', d = 1;
                    while (i < s.length && d > 0) { if (s[i] === '(') d++; else if (s[i] === ')') d--; if (d > 0) params += s[i]; i++; }
                    const local = { ...state };
                    const fill = extractParamValue(params, 'fill'); if (fill) local.fillColor = getColor(fill);
                    const sz = extractParamValue(params, 'size');
                    if (sz) local.fontSize = parseSize(sz);
                    else { const m = /([\d.]+)\s*(?:pt|em)/.exec(params); if (m) local.fontSize = parseSize(m[0]); }
                    if (params.includes('bold')) local.isBold = true;
                    while (i < s.length && (s[i] === ' ' || s[i] === '\t')) i++;
                    if (s[i] === '[') {
                        i++; let bd = 1, inner = '';
                        while (i < s.length && bd > 0) { if (s[i] === '[') bd++; else if (s[i] === ']') bd--; if (bd > 0) inner += s[i]; i++; }
                        runs.push(...parseInlineRuns(inner, local));
                    }
                    continue;
                }
                buf += s[i++];
            }
            flush();
            return runs;
        }

        function getCharWidth(ch, fontSize, isBold) {
            const code = ch.charCodeAt(0) & 0xFF;
            let w = (CHAR_W[code] || 500) / 1000.0 * fontSize;
            if (isBold) w *= 1.05;
            return w;
        }

        function pdfEscape(str) {
            return sanitizeText(str).replace(/([()\\])/g, '\\$1');
        }

        // Parse full document AST
        const doc = makeNode('PAGE');
        while (skipWS()) {
            const prev = pos;
            const el = parseElement();
            if (el) doc.children.push(el);
            if (pos === prev) pos++;
        }

        // Render AST into PDF streams + Display List
        const pages = [''];
        const dlItems = [];
        let curPage = 0, disablePageBreaks = false;
        const emit = (s) => { pages[curPage] += s; };

        function drawTextRun(text, x, y, fontSize, isBold, isItalic, color, angleDeg = 0) {
            dlItems.push({ type: 'TEXT', page: curPage, x, y, fontSize, isBold, isItalic, c: color, text, angle: angleDeg });
            const font = (isBold && isItalic) ? 'F3' : isBold ? 'F2' : isItalic ? 'F4' : 'F1';
            const rad = angleDeg * Math.PI / 180;
            const a = Math.cos(rad), b = Math.sin(rad), c = -Math.sin(rad), d = Math.cos(rad);
            emit(`BT\n/${font} ${fontSize.toFixed(1)} Tf\n${(color.r/255).toFixed(3)} ${(color.g/255).toFixed(3)} ${(color.b/255).toFixed(3)} rg\n${a.toFixed(4)} ${b.toFixed(4)} ${c.toFixed(4)} ${d.toFixed(4)} ${x.toFixed(2)} ${y.toFixed(2)} Tm\n(${pdfEscape(text)}) Tj\nET\n0 0 0 rg\n`);
        }

        function drawRect(x, y, w, h, fillC, hasStroke, strokeC, strokeW, radius = 0) {
            const hasFill = fillC && fillC.name !== 'none';
            dlItems.push({ type: radius > 0.5 ? 'ROUND_RECT' : 'RECT', page: curPage, x, y: y - h, w, h, radius, c: hasFill ? fillC : { r:255,g:255,b:255,name:'none' }, strokeC: hasStroke ? strokeC : { r:0,g:0,b:0,name:'none' }, strokeW: hasStroke ? strokeW : 0 });
            if (radius > 0.5) {
                const r = Math.min(radius, w / 2, h / 2), k = 0.5522847498 * r;
                const x0 = x, y0 = y - h, x1 = x + w, y1 = y;
                if (hasFill) emit(`${(fillC.r/255).toFixed(3)} ${(fillC.g/255).toFixed(3)} ${(fillC.b/255).toFixed(3)} rg\n`);
                if (hasStroke) emit(`${(strokeC.r/255).toFixed(3)} ${(strokeC.g/255).toFixed(3)} ${(strokeC.b/255).toFixed(3)} RG ${strokeW.toFixed(2)} w\n`);
                emit(`${(x0+r).toFixed(2)} ${y0.toFixed(2)} m\n${(x1-r).toFixed(2)} ${y0.toFixed(2)} l\n`);
                emit(`${(x1-r+k).toFixed(2)} ${y0.toFixed(2)} ${x1.toFixed(2)} ${(y0+r-k).toFixed(2)} ${x1.toFixed(2)} ${(y0+r).toFixed(2)} c\n`);
                emit(`${x1.toFixed(2)} ${(y1-r).toFixed(2)} l\n${x1.toFixed(2)} ${(y1-r+k).toFixed(2)} ${(x1-r+k).toFixed(2)} ${y1.toFixed(2)} ${(x1-r).toFixed(2)} ${y1.toFixed(2)} c\n`);
                emit(`${(x0+r).toFixed(2)} ${y1.toFixed(2)} l\n${(x0+r-k).toFixed(2)} ${y1.toFixed(2)} ${x0.toFixed(2)} ${(y1-r+k).toFixed(2)} ${x0.toFixed(2)} ${(y1-r).toFixed(2)} c\n`);
                emit(`${x0.toFixed(2)} ${(y0+r).toFixed(2)} l\n${x0.toFixed(2)} ${(y0+r-k).toFixed(2)} ${(x0+r-k).toFixed(2)} ${y0.toFixed(2)} ${(x0+r).toFixed(2)} ${y0.toFixed(2)} c\nh `);
                emit(hasFill && hasStroke ? 'B\n' : hasFill ? 'f\n' : hasStroke ? 'S\n' : 'n\n');
                if (hasStroke) emit('0 0 0 RG\n');
                return;
            }
            if (hasFill) {
                emit(`${(fillC.r/255).toFixed(3)} ${(fillC.g/255).toFixed(3)} ${(fillC.b/255).toFixed(3)} rg\n`);
                emit(`${x.toFixed(2)} ${(y - h).toFixed(2)} ${w.toFixed(2)} ${h.toFixed(2)} re f\n`);
            }
            if (hasStroke) {
                emit(`${(strokeC.r/255).toFixed(3)} ${(strokeC.g/255).toFixed(3)} ${(strokeC.b/255).toFixed(3)} RG ${strokeW.toFixed(2)} w\n`);
                emit(`${x.toFixed(2)} ${(y - h).toFixed(2)} ${w.toFixed(2)} ${h.toFixed(2)} re S\n0 0 0 RG\n`);
            }
        }

        function drawLine(x1, y1, x2, y2, color, sw) {
            dlItems.push({ type: 'LINE', page: curPage, x: x1, y: y1, w: x2 - x1, h: y2 - y1, c: color, strokeW: sw });
            emit(`${(color.r/255).toFixed(3)} ${(color.g/255).toFixed(3)} ${(color.b/255).toFixed(3)} RG ${sw.toFixed(2)} w\n${x1.toFixed(2)} ${y1.toFixed(2)} m ${x2.toFixed(2)} ${y2.toFixed(2)} l S\n0 0 0 RG\n`);
        }

        function renderStyledText(text, x, y, maxW, fontSize, align, defaultColor, dryRun) {
            const runs = parseInlineRuns(text, { fontSize, fillColor: defaultColor, isBold: false, isItalic: false });
            let maxRunSize = fontSize;
            runs.forEach(r => { if (r.fontSize > maxRunSize) maxRunSize = r.fontSize; });
            let curY = y - maxRunSize, startX = x;

            if (align > 0) {
                let estW = 0;
                runs.forEach(r => { for (const ch of r.text) if (ch !== '\n') estW += getCharWidth(ch, r.fontSize, r.isBold); });
                if (estW > 0 && estW < maxW) startX += align === 1 ? (maxW - estW) / 2 : (maxW - estW);
            }
            let drawX = startX;
            for (const run of runs) {
                if (run.text === '\n') { curY -= (maxRunSize + 4); drawX = startX; continue; }
                const tokens = run.text.split(/([ \t]+)/);
                let lineBuf = '', fragStartX = drawX;
                for (const tok of tokens) {
                    if (!tok) continue;
                    let tw = 0; for (const ch of tok) tw += getCharWidth(ch, run.fontSize, run.isBold);
                    const isSpace = /^[ \t]+$/.test(tok);
                    if (!isSpace && drawX + tw > startX + maxW && drawX > startX) {
                        if (lineBuf && !dryRun) drawTextRun(lineBuf, fragStartX, curY, run.fontSize, run.isBold, run.isItalic, run.fillColor, 0);
                        lineBuf = ''; curY -= (maxRunSize + 4); drawX = startX; fragStartX = startX;
                    }
                    lineBuf += tok; drawX += tw;
                }
                if (lineBuf && !dryRun) drawTextRun(lineBuf, fragStartX, curY, run.fontSize, run.isBold, run.isItalic, run.fillColor, 0);
            }
            return (y - curY) + 4.0;
        }

        function calcColWidths(n, maxW, includeGutter) {
            let fixed = 0, fr = 0;
            for (let i = 0; i < n.cellCols; i++) { if (n.isFr[i]) fr += n.colWidths[i]; else fixed += n.colWidths[i]; }
            const avail = maxW - fixed - (includeGutter ? (n.cellCols - 1) * n.gutter : 0);
            const frUnit = fr > 0 ? avail / fr : 0;
            return n.colWidths.map((w, i) => n.isFr[i] ? w * frUnit : w);
        }

        function measureHeight(n, maxW) {
            if (!n) return 0;
            const rw = n.hasWidth === 2 ? n.width * maxW : (n.hasWidth === 1 ? n.width : maxW);
            switch (n.type) {
                case 'VSPACE': return n.height;
                case 'LINE': return 10;
                case 'PARAGRAPH': case 'HEADING': return renderStyledText(n.content, 0, 0, maxW, n.fontSize, n.align, currentState.fillColor, true);
                case 'BLOCK': return n.children.length ? n.children.reduce((s, c) => s + measureHeight(c, rw), 0) : renderStyledText(n.content, 0, 0, rw, n.fontSize, n.align, currentState.fillColor, true);
                case 'RECT': {
                    const ins = n.inset > 0 ? n.inset : 8;
                    const ch = n.children.length ? n.children.reduce((s, c) => s + measureHeight(c, rw - ins * 2), 0) : renderStyledText(n.content, 0, 0, rw - ins * 2, n.fontSize, 0, currentState.fillColor, true);
                    return ch + ins * 2;
                }
                case 'TABLE': case 'GRID': {
                    const isGrid = n.type === 'GRID';
                    const widths = calcColWidths(n, maxW, isGrid);
                    const ins = isGrid ? 0 : (n.inset > 0 ? n.inset : 6);
                    let total = 0;
                    for (let r = 0; r < n.cellRows; r++) {
                        let maxH = 0;
                        for (let c = 0; c < n.cellCols; c++) {
                            const cell = n.cells[r]?.[c];
                            if (cell) maxH = Math.max(maxH, measureHeight(cell, widths[c] - ins * 2));
                        }
                        total += maxH + ins * 2;
                    }
                    return total;
                }
                case 'PIE_CHART': return 260;
                case 'LINE_CHART': case 'BAR_CHART': return (n.chartXLabel ? 225 : 200) + (n.chartTitle ? 25 : 0);
                case 'TSCORE_CHART': case 'BMD_CHART': return 105;
                case 'BARCODE': return n.hasHeight === 2 ? n.height * maxW : (n.hasHeight === 1 ? n.height : 50);
                default: return 16;
            }
        }

        function renderNode(n, yRef, maxW, startX) {
            if (!n) return;
            if (n.type === 'PAGEBREAK') {
                curPage++; pages.push(''); yRef.y = pageHeight - marginTop; return;
            }
            if (yRef.y < marginBottom + 20 && !disablePageBreaks) {
                curPage++; pages.push(''); yRef.y = pageHeight - marginTop;
            }
            const rw = n.hasWidth === 2 ? n.width * maxW : (n.hasWidth === 1 ? n.width : maxW);

            switch (n.type) {
                case 'HEADING': {
                    const h = renderStyledText(n.content, startX, yRef.y, rw, n.fontSize, n.align, getColor('#2B6CB0'), false);
                    yRef.y -= (h + 8); break;
                }
                case 'PARAGRAPH': {
                    const h = renderStyledText(n.content, startX, yRef.y, rw, n.fontSize, n.align, currentState.fillColor, false);
                    yRef.y -= h; break;
                }
                case 'BLOCK': {
                    let bx = startX;
                    if (n.align === 1) bx = startX + (maxW - rw) / 2;
                    else if (n.align === 2) bx = startX + (maxW - rw);
                    n.children.forEach(c => renderNode(c, yRef, rw, bx));
                    break;
                }
                case 'LINE': {
                    drawLine(startX, yRef.y, startX + rw, yRef.y, n.hasStroke ? n.strokeColor : getColor('#CBD5E0'), n.strokeWidth || 0.5);
                    yRef.y -= 8; break;
                }
                case 'VSPACE': yRef.y -= n.height; break;
                case 'RECT': {
                    const ins = n.inset > 0 ? n.inset : 8;
                    const ch = n.children.length ? n.children.reduce((s, c) => s + measureHeight(c, rw - ins * 2), 0)
                        : (n.content ? renderStyledText(n.content, 0, 0, rw - ins * 2, n.fontSize, 0, currentState.fillColor, true) : 10);
                    const totalH = ch + ins * 2;
                    drawRect(startX, yRef.y, rw, totalH, n.fillColor, n.hasStroke, n.strokeColor, n.strokeWidth, n.radius);
                    if (n.children.length) {
                        const cy = { y: yRef.y - ins };
                        n.children.forEach(c => renderNode(c, cy, rw - ins * 2, startX + ins));
                    } else if (n.content) {
                        renderStyledText(n.content, startX + ins, yRef.y - ins, rw - ins * 2, n.fontSize, 0, currentState.fillColor, false);
                    }
                    yRef.y -= (totalH + 8); break;
                }
                case 'GRID': {
                    const widths = calcColWidths(n, rw, true);
                    for (let r = 0; r < n.cellRows; r++) {
                        let maxH = 0, cx = startX;
                        for (let c = 0; c < n.cellCols; c++) {
                            const cell = n.cells[r]?.[c];
                            if (cell) maxH = Math.max(maxH, measureHeight(cell, widths[c]));
                        }
                        for (let c = 0; c < n.cellCols; c++) {
                            const cell = n.cells[r]?.[c];
                            if (cell) { const cy = { y: yRef.y }; renderNode(cell, cy, widths[c], cx); }
                            cx += widths[c] + n.gutter;
                        }
                        yRef.y -= maxH;
                    }
                    break;
                }
                case 'TABLE': {
                    const widths = calcColWidths(n, rw, false);
                    const totalW = widths.reduce((a, b) => a + b, 0);
                    const ins = n.inset > 0 ? n.inset : 6;
                    const startY = yRef.y, rowYs = [startY];

                    for (let r = 0; r < n.cellRows; r++) {
                        let maxH = 0;
                        for (let c = 0; c < n.cellCols; c++) {
                            const cell = n.cells[r]?.[c];
                            if (cell) maxH = Math.max(maxH, measureHeight(cell, widths[c] - ins * 2));
                        }
                        const boxH = maxH + ins * 2;
                        const rowFill = (r === 0 && n.altRows) ? n.headerFill : n.altRows ? (r % 2 === 1 ? n.stripe1 : n.stripe2) : n.fillColor;
                        let cx = startX;
                        for (let c = 0; c < n.cellCols; c++) {
                            drawRect(cx, yRef.y, widths[c], boxH, rowFill, false, rowFill, 0);
                            const cell = n.cells[r]?.[c];
                            if (cell) {
                                const oldCol = currentState.fillColor;
                                currentState.fillColor = (r === 0 && n.altRows) ? getColor('white') : oldCol;
                                const cy = { y: yRef.y - ins };
                                renderNode(cell, cy, widths[c] - ins * 2, cx + ins);
                                currentState.fillColor = oldCol;
                            }
                            cx += widths[c];
                        }
                        yRef.y -= boxH;
                        rowYs.push(yRef.y);
                    }
                    if (n.hasStroke) {
                        drawRect(startX, startY, totalW, startY - yRef.y, { r:255,g:255,b:255,name:'none' }, true, n.strokeColor, n.strokeWidth);
                        for (let r = 1; r < n.cellRows; r++) drawLine(startX, rowYs[r], startX + totalW, rowYs[r], n.strokeColor, n.strokeWidth);
                        let cx = startX;
                        for (let c = 1; c < n.cellCols; c++) { cx += widths[c - 1]; drawLine(cx, startY, cx, yRef.y, n.strokeColor, n.strokeWidth); }
                    }
                    break;
                }
                case 'BAR_CHART': case 'LINE_CHART': {
                    const hasX = !!n.chartXLabel, hasY = !!n.chartYLabel, hasT = !!n.chartTitle;
                    const h = hasX ? 225 : 200, w = 440;
                    const xStart = hasY ? 65 : 50, xEnd = 415, plotW = xEnd - xStart, yTop = 20, yBot = 150, plotH = yBot - yTop;
                    const ox = Math.max(startX, startX + (rw - w) / 2);
                    let oy = yRef.y;
                    if (hasT) { renderStyledText(n.chartTitle, ox, oy - 15, w, 12, 1, getColor('black'), false); oy -= 25; }
                    const rawMax = Math.max(0, ...n.chartScores);
                    const maxVal = Math.max(10, Math.ceil(rawMax / 10) * 10);
                    for (let idx = 0; idx < 5; idx++) {
                        const val = (maxVal / 4) * (4 - idx), ly = oy - (yTop + idx * (plotH / 4));
                        if (idx < 4) drawLine(ox + xStart, ly, ox + xEnd, ly, { r:229,g:231,b:235,name:'g' }, 1);
                        const lb = String(Math.round(val));
                        let lw = 0; for (const ch of lb) lw += getCharWidth(ch, 10, false);
                        drawTextRun(lb, ox + xStart - 8 - lw, ly - 3, 10, false, false, { r:102,g:102,b:102,name:'t' });
                    }
                    drawLine(ox + xStart, oy - yBot, ox + xEnd, oy - yBot, { r:51,g:51,b:51,name:'a' }, 2);
                    drawLine(ox + xStart, oy - yTop, ox + xStart, oy - yBot, { r:51,g:51,b:51,name:'a' }, 2);
                    const cnt = n.chartLabels.length;
                    if (cnt > 0) {
                        const slotW = plotW / cnt, barW = Math.min(35, slotW * 0.7);
                        const pal = ['#2563eb','#16a34a','#d97706','#dc2626','#7c3aed','#06b6d4','#db2777','#4f46e5'];
                        const defC = getColor(n.chartColor);
                        const pts = [];
                        for (let i = 0; i < cnt; i++) {
                            const sc = xStart + i * slotW + slotW / 2, bh = (n.chartScores[i] / maxVal) * plotH, by = oy - yBot + bh;
                            const col = n.chartColor === 'random' ? getColor(pal[i % 8]) : defC;
                            if (n.type === 'BAR_CHART' && bh > 0) drawRect(ox + sc - barW / 2, by, barW, bh, col, false, col, 0);
                            pts.push([ox + sc, by]);
                            const vb = String(Math.round(n.chartScores[i]));
                            let vw = 0; for (const ch of vb) vw += getCharWidth(ch, 10, false);
                            drawTextRun(vb, ox + sc - vw / 2, by + (n.type === 'BAR_CHART' ? 6 : 8), 10, false, false, col);
                            let lw = 0; for (const ch of n.chartLabels[i]) lw += getCharWidth(ch, 11, false);
                            drawTextRun(n.chartLabels[i], ox + sc - lw / 2, oy - yBot - 20, 11, false, false, { r:102,g:102,b:102,name:'t' });
                        }
                        if (n.type === 'LINE_CHART') {
                            if (n.chartTrend && cnt > 1) {
                                let sx = 0, sy = 0, sxx = 0, sxy = 0;
                                for (let i = 0; i < cnt; i++) { sx += i; sy += n.chartScores[i]; sxx += i * i; sxy += i * n.chartScores[i]; }
                                const den = cnt * sxx - sx * sx;
                                if (den) {
                                    const m = (cnt * sxy - sx * sy) / den, b = (sy - m * sx) / cnt;
                                    drawLine(pts[0][0], oy - yBot + (b / maxVal) * plotH, pts[cnt-1][0], oy - yBot + ((m * (cnt - 1) + b) / maxVal) * plotH, { r:220,g:38,b:38,name:'r' }, 2);
                                }
                            }
                            for (let i = 0; i < cnt - 1; i++) drawLine(pts[i][0], pts[i][1], pts[i+1][0], pts[i+1][1], defC, 2.5);
                            for (let i = 0; i < cnt; i++) drawRect(pts[i][0] - 2, pts[i][1] + 2, 4, 4, defC, false, defC, 0);
                        }
                    }
                    if (hasY) {
                        let tw = 0; for (const ch of n.chartYLabel) tw += getCharWidth(ch, 11, true);
                        drawTextRun(n.chartYLabel, ox + 20, oy - (yTop + plotH / 2) - tw / 2, 11, true, false, { r:51,g:51,b:51,name:'a' }, 90);
                    }
                    if (hasX) {
                        let tw = 0; for (const ch of n.chartXLabel) tw += getCharWidth(ch, 11, true);
                        drawTextRun(n.chartXLabel, ox + xStart + plotW / 2 - tw / 2, oy - yBot - 42, 11, true, false, { r:51,g:51,b:51,name:'a' }, 0);
                    }
                    yRef.y -= (hasT ? h + 25 : h); break;
                }
                case 'PIE_CHART': {
                    const cx = startX + 140, cy = yRef.y - 120, r = 85;
                    if (n.chartTitle) renderStyledText(n.chartTitle, startX, yRef.y - 15, rw, n.fontSize + 4, 1, currentState.fillColor, false);
                    const pal = ['#2563eb','#16a34a','#d97706','#dc2626','#7c3aed','#06b6d4','#db2777','#4f46e5'].map(getColor);
                    const tot = n.chartScores.reduce((a, b) => a + b, 0) || 1;
                    let ang = -Math.PI / 2;
                    n.chartScores.forEach((sc, i) => {
                        const slice = (sc / tot) * 2 * Math.PI;
                        if (slice < 0.001) return;
                        const end = ang + slice, col = pal[i % 8];
                        dlItems.push({ type: 'PIE', page: curPage, x: cx - r, y: cy - r, w: r * 2, h: r * 2, c: col, startAngle: ang, endAngle: end });
                        emit(`${(col.r/255).toFixed(3)} ${(col.g/255).toFixed(3)} ${(col.b/255).toFixed(3)} rg\n1 1 1 RG 1.5 w\n${cx.toFixed(2)} ${cy.toFixed(2)} m\n${(cx + r*Math.cos(ang)).toFixed(2)} ${(cy - r*Math.sin(ang)).toFixed(2)} l\n`);
                        const segs = Math.max(1, Math.ceil(slice / (Math.PI / 2))), step = slice / segs;
                        let ca = ang;
                        for (let s = 0; s < segs; s++) {
                            const a0 = ca, a1 = ca + step, a = (a1 - a0) / 2, k = (4 / 3) * (1 - Math.cos(a)) / Math.sin(a);
                            const x1 = cx + r * Math.cos(a1), y1 = cy - r * Math.sin(a1);
                            const cp1x = (cx + r * Math.cos(a0)) + k * (-r * Math.sin(a0)), cp1y = (cy - r * Math.sin(a0)) + k * (-r * Math.cos(a0));
                            const cp2x = x1 - k * (-r * Math.sin(a1)), cp2y = y1 - k * (-r * Math.cos(a1));
                            emit(`${cp1x.toFixed(2)} ${cp1y.toFixed(2)} ${cp2x.toFixed(2)} ${cp2y.toFixed(2)} ${x1.toFixed(2)} ${y1.toFixed(2)} c\n`);
                            ca = a1;
                        }
                        emit('h B\n');
                        const ly = (yRef.y - 60) - i * 25, lx = startX + 270;
                        drawRect(lx, ly + 3, 14, 14, col, false, col, 0);
                        renderStyledText(`${n.chartLabels[i]} (${sc} - ${(sc / tot * 100).toFixed(1)}%)`, lx + 25, ly, 200, 11, 0, { r:51,g:51,b:51,name:'d' }, false);
                        ang = end;
                    });
                    yRef.y -= 260; break;
                }
                case 'TSCORE_CHART': case 'BMD_CHART': {
                    const h = 105, topY = yRef.y;
                    drawRect(startX, topY, rw, h, getColor('#FCFCFC'), true, getColor('#CBD5E0'), 0.5);
                    renderStyledText(`*${n.chartTitle}*`, startX + 8, topY - 12, rw - 16, 8.5, 0, getColor('#1A365D'), false);
                    const px = startX + 10, pyTop = topY - 24, ph = 64, pw = rw - 20;
                    drawRect(px, pyTop, pw, ph * 0.375, getColor('#F0FFF4'), false, getColor('#F0FFF4'), 0);
                    drawRect(px, pyTop - ph * 0.375, pw, ph * 0.375, getColor('#FFFAF0'), false, getColor('#FFFAF0'), 0);
                    drawRect(px, pyTop - ph * 0.75, pw, ph * 0.25, getColor('#FFF5F5'), false, getColor('#FFF5F5'), 0);
                    drawLine(px, pyTop - ph * 0.375, px + pw, pyTop - ph * 0.375, getColor('#38A169'), 0.5);
                    drawLine(px, pyTop - ph * 0.75, px + pw, pyTop - ph * 0.75, getColor('#E53E3E'), 0.5);
                    const cnt = n.chartLabels.length || 5, colW = pw / cnt, barW = Math.min(16, colW * 0.6);
                    for (let i = 0; i < cnt; i++) {
                        const val = n.chartScores[i] ?? 0, cl = Math.max(-3, Math.min(1, val));
                        const zeroY = pyTop - ph * 0.375, scY = pyTop - ph * ((1 - cl) / 4);
                        const topB = Math.max(scY, zeroY), bh = Math.max(1, Math.abs(scY - zeroY));
                        const bc = val <= -2.5 ? getColor('#E53E3E') : val <= -1.0 ? getColor('#DD6B20') : getColor('#38A169');
                        drawRect(px + (i + 0.5) * colW - barW / 2, topB, barW, bh, bc, false, bc, 0);
                        renderStyledText(`*${val.toFixed(1)}*`, px + i * colW, Math.max(pyTop - ph + 10, Math.min(pyTop - 2, val >= 0 ? topB + 8 : topB - bh - 2)), colW, 6.5, 1, getColor('#1A365D'), false);
                        renderStyledText(`*${n.chartLabels[i]}*`, px + i * colW, pyTop - ph - 2, colW, 6.5, 1, getColor('#2D3748'), false);
                    }
                    yRef.y -= h; break;
                }
                case 'BARCODE': {
                    const reqH = n.hasHeight === 2 ? n.height * maxW : (n.hasHeight === 1 ? n.height : 50);
                    const cx = startX + (rw - rw) / 2;
                    const blk = getColor('black');
                    if (n.barcodeType.includes('upc')) {
                        let d = n.barcodeData.replace(/\D/g, '').slice(0, 12);
                        if (d.length === 11) {
                            let odd = 0, even = 0;
                            for (let i = 0; i < 11; i++) { if (i % 2 === 0) odd += +d[i]; else even += +d[i]; }
                            const chk = (odd * 3 + even) % 10; d += String(chk === 0 ? 0 : 10 - chk);
                        }
                        if (d.length === 12) {
                            const mw = rw / 95; let xo = 0;
                            const drawSeq = (s) => { for (const b of s) { if (b === '1') drawRect(cx + xo, yRef.y, mw, reqH, blk, false, blk, 0); xo += mw; } };
                            drawSeq('101'); for (let i = 0; i < 6; i++) drawSeq(UPC_L[+d[i]]);
                            drawSeq('01010'); for (let i = 6; i < 12; i++) drawSeq(UPC_R[+d[i]]); drawSeq('101');
                        }
                    } else if (n.barcodeType.includes('128')) {
                        const data = n.barcodeData;
                        if (data.length) {
                            const mw = rw / (35 + 11 * data.length); let xo = 0, sum = 104;
                            const drawPat = (idx) => { const p = C128_PAT[idx]; for (let i = 0; i < p.length; i++) { const w = (+p[i]) * mw; if (i % 2 === 0) drawRect(cx + xo, yRef.y, w, reqH, blk, false, blk, 0); xo += w; } };
                            drawPat(104);
                            for (let i = 0; i < data.length; i++) { let v = data.charCodeAt(i) - 32; if (v < 0 || v > 95) v = 0; drawPat(v); sum += v * (i + 1); }
                            drawPat(sum % 103); drawPat(106);
                        }
                    } else if (n.barcodeType.includes('qr')) {
                        const qr = generateQrMatrix(n.barcodeData);
                        if (qr) {
                            const mw = Math.min(rw / qr.size, reqH / qr.size);
                            const bx = cx + (rw - qr.size * mw) / 2, by = yRef.y - (reqH - qr.size * mw) / 2;
                            for (let ry = 0; ry < qr.size; ry++) for (let rx = 0; rx < qr.size; rx++)
                                if (qr.sym[ry][rx] === 1) drawRect(bx + rx * mw, by - ry * mw, mw, mw, blk, false, blk, 0);
                        }
                    } else if (n.barcodeType.includes('datamatrix') || n.barcodeType.includes('dm')) {
                        const dm = buildDataMatrix(n.barcodeData);
                        if (dm) {
                            const mw = Math.min(rw / dm.size, reqH / dm.size);
                            const bx = cx + (rw - dm.size * mw) / 2, by = yRef.y - (reqH - dm.size * mw) / 2;
                            for (let ry = 0; ry < dm.size; ry++) for (let rx = 0; rx < dm.size; rx++)
                                if (dm.bmp[ry][rx] === 1) drawRect(bx + rx * mw, by - ry * mw, mw, mw, blk, false, blk, 0);
                        }
                    } else if (n.barcodeType.includes('pdf417')) {
                        const p417 = buildPdf417(n.barcodeData);
                        if (p417) {
                            const { fullBuf, rows, cols, eccLevel, modulesPerRow } = p417;
                            const mw = rw / modulesPerRow;
                            const mh = reqH / rows;
                            const startPat = [1,1,1,1,1,1,1,1,0,1,0,1,0,1,0,0,0];
                            const stopPat  = [1,1,1,1,1,1,1,0,1,0,0,0,1,0,1,0,0,1];

                            for (let ry = 0; ry < rows; ry++) {
                                let xo = 0;
                                const cluster = ry % 3, v = Math.floor(ry / 3);
                                let leftInd, rightInd;
                                if (cluster === 0) {
                                    leftInd = 30 * v + Math.floor((rows - 1) / 3);
                                    rightInd = 30 * v + (cols - 1);
                                } else if (cluster === 1) {
                                    leftInd = 30 * v + eccLevel * 3 + ((rows - 1) % 3);
                                    rightInd = 30 * v + Math.floor((rows - 1) / 3);
                                } else {
                                    leftInd = 30 * v + (cols - 1);
                                    rightInd = 30 * v + eccLevel * 3 + ((rows - 1) % 3);
                                }

                                const rowSeq = [leftInd];
                                for (let c = 0; c < cols; c++) rowSeq.push(fullBuf[ry * cols + c]);
                                rowSeq.push(rightInd);

                                for (let i = 0; i < 17; i++) {
                                    if (startPat[i]) drawRect(cx + xo * mw, yRef.y - ry * mh, mw, mh, blk, false, blk, 0);
                                    xo++;
                                }
                                for (let i = 0; i < rowSeq.length; i++) {
                                    const pat = PDF417_CLUSTERS[cluster][rowSeq[i]];
                                    for (let bit = 16; bit >= 0; bit--) {
                                        if ((pat >> bit) & 1) drawRect(cx + xo * mw, yRef.y - ry * mh, mw, mh, blk, false, blk, 0);
                                        xo++;
                                    }
                                }
                                for (let i = 0; i < 18; i++) {
                                    if (stopPat[i]) drawRect(cx + xo * mw, yRef.y - ry * mh, mw, mh, blk, false, blk, 0);
                                    xo++;
                                }
                            }
                        }
                    }
                    yRef.y -= (reqH + 8); break;
                }
            }
        }

        const yRef = { y: pageHeight - marginTop };
        const maxW = pageWidth - marginLeft - marginRight;
        doc.children.forEach(c => renderNode(c, yRef, maxW, marginLeft));

        // Render dynamic headers & footers on every page
        const numPages = pages.length;
        disablePageBreaks = true;
        for (let p = 0; p < numPages; p++) {
            curPage = p;
            if (headerScript) {
                const hNode = parseTypstString(headerScript.replace(/%d/g, String(p + 1)));
                if (hNode) renderNode(hNode, { y: pageHeight - marginTop / 2 }, maxW, marginLeft);
            }
            if (footerScript) {
                const fNode = parseTypstString(footerScript.replace(/%d/g, String(p + 1)));
                if (fNode) renderNode(fNode, { y: marginBottom / 2 }, maxW, marginLeft);
            }
        }
        disablePageBreaks = false;

        return { pages, dlItems, pageWidth, pageHeight, numPages };
    }

    /* ========================================================================
     * PDF 1.4 BINARY SERIALIZER (In-Memory, Zero Server Storage)
     * ======================================================================== */
    function buildPdfBytes(compiled) {
        const { pages, pageWidth, pageHeight, numPages } = compiled;
        const chunks = [];
        const offsets = [0];
        let byteLen = 0;

        const pushStr = (s) => {
            const arr = new Uint8Array(s.length);
            for (let i = 0; i < s.length; i++) arr[i] = s.charCodeAt(i) & 0xFF;
            chunks.push(arr);
            byteLen += arr.length;
        };

        pushStr('%PDF-1.4\n%\xE2\xE3\xCF\xD3\n');
        offsets.push(byteLen);
        pushStr('1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n');

        offsets.push(byteLen);
        const kids = Array.from({ length: numPages }, (_, i) => `${7 + i * 2} 0 R`).join(' ');
        pushStr(`2 0 obj\n<< /Type /Pages /Kids [${kids}] /Count ${numPages} >>\nendobj\n`);

        const fonts = ['Helvetica', 'Helvetica-Bold', 'Helvetica-BoldOblique', 'Helvetica-Oblique'];
        fonts.forEach((f, idx) => {
            offsets.push(byteLen);
            pushStr(`${3 + idx} 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /${f} >>\nendobj\n`);
        });

        for (let i = 0; i < numPages; i++) {
            const pageObj = 7 + i * 2, streamObj = pageObj + 1;
            offsets.push(byteLen);
            pushStr(`${pageObj} 0 obj\n<< /Type /Page /Parent 2 0 R /Resources << /Font << /F1 3 0 R /F2 4 0 R /F3 5 0 R /F4 6 0 R >> >> /MediaBox [0 0 ${pageWidth.toFixed(2)} ${pageHeight.toFixed(2)}] /Contents ${streamObj} 0 R >>\nendobj\n`);
            offsets.push(byteLen);
            pushStr(`${streamObj} 0 obj\n<< /Length ${pages[i].length} >>\nstream\n${pages[i]}endstream\nendobj\n`);
        }

        const xrefPos = byteLen;
        pushStr(`xref\n0 ${offsets.length}\n0000000000 65535 f \n`);
        for (let i = 1; i < offsets.length; i++) {
            pushStr(`${String(offsets[i]).padStart(10, '0')} 00000 n \n`);
        }
        pushStr(`trailer\n<< /Size ${offsets.length} /Root 1 0 R >>\nstartxref\n${xrefPos}\n%%EOF\n`);

        const out = new Uint8Array(byteLen);
        let off = 0;
        for (const c of chunks) { out.set(c, off); off += c.length; }
        return out;
    }

    function compileToPdfBytes(typstSource) {
        return buildPdfBytes(compileDocument(typstSource));
    }

    function compileToPdfBlob(typstSource) {
        return new Blob([compileToPdfBytes(typstSource)], { type: 'application/pdf' });
    }

    function downloadPdf(typstSource, filename = 'report.pdf') {
        const url = URL.createObjectURL(compileToPdfBlob(typstSource));
        const a = document.createElement('a');
        a.href = url; a.download = filename; a.click();
        setTimeout(() => URL.revokeObjectURL(url), 5000);
    }

    function openPdfInNewTab(typstSource) {
        const url = URL.createObjectURL(compileToPdfBlob(typstSource));
        window.open(url, '_blank', 'noopener');
    }

    /* ========================================================================
     * INTERACTIVE HTML5 CANVAS VIEWER (Mirrors Win32 GDI Viewer)
     * ======================================================================== */
    function mountViewer(containerId, typstSource, opts = {}) {
        const host = typeof containerId === 'string' ? document.getElementById(containerId) : containerId;
        if (!host) return;

        let compiled = compileDocument(typstSource);
        let curPage = 0, zoomMode = 'FIT_PAGE', customZoom = 1.0, panX = 0, panY = 0;
        let isPanning = false, dragX = 0, dragY = 0, searchIdx = -1;

        host.innerHTML = `
        <div class="tj-root" style="display:flex;flex-direction:column;width:100%;height:100%;background:#e6e6e6;font-family:sans-serif;overflow:hidden;">
            <div style="background:#2c3e50;color:#fff;padding:6px 10px;display:flex;flex-wrap:wrap;gap:6px;align-items:center;font-size:12px;">
                <button type="button" class="tj-first">|&lt;</button>
                <button type="button" class="tj-prev">&lt;</button>
                <span class="tj-pg-lbl">1 / ${compiled.numPages}</span>
                <button type="button" class="tj-next">&gt;</button>
                <button type="button" class="tj-last">&gt;|</button>
                <input type="text" class="tj-srch" placeholder="Search..." style="width:110px;padding:2px 5px;font-size:12px;">
                <button type="button" class="tj-srch-p">Prev</button>
                <button type="button" class="tj-srch-n">Next</button>
                <button type="button" class="tj-fitw">Fit Width</button>
                <button type="button" class="tj-fitp">Fit Page</button>
                <button type="button" class="tj-zout">-</button>
                <span class="tj-zlbl">100%</span>
                <button type="button" class="tj-zin">+</button>
                <button type="button" class="tj-pdf" style="margin-left:auto;background:#27ae60;color:#fff;border:none;padding:4px 10px;border-radius:3px;cursor:pointer;font-weight:bold;">Download PDF</button>
            </div>
            <div class="tj-stage" style="flex:1;position:relative;overflow:hidden;cursor:grab;">
                <canvas class="tj-cvs" style="display:block;width:100%;height:100%;"></canvas>
            </div>
        </div>`;

        const stage = host.querySelector('.tj-stage');
        const cvs = host.querySelector('.tj-cvs');
        const ctx = cvs.getContext('2d');
        const pgLbl = host.querySelector('.tj-pg-lbl');
        const zLbl = host.querySelector('.tj-zlbl');
        const srchIn = host.querySelector('.tj-srch');

        function getScale() {
            const w = stage.clientWidth || 800, h = stage.clientHeight || 600;
            if (zoomMode === 'FIT_PAGE') return Math.max(0.1, Math.min((w - 30) / compiled.pageWidth, (h - 30) / compiled.pageHeight));
            if (zoomMode === 'FIT_WIDTH') return Math.max(0.1, (w - 30) / compiled.pageWidth);
            return Math.max(0.1, customZoom);
        }

        function paint() {
            const w = stage.clientWidth || 800, h = stage.clientHeight || 600;
            const dpr = window.devicePixelRatio || 1;
            cvs.width = w * dpr; cvs.height = h * dpr;
            ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

            const sc = getScale();
            zLbl.innerText = Math.round(sc * 100) + '%';
            pgLbl.innerText = `${curPage + 1} / ${compiled.numPages}`;

            ctx.fillStyle = '#e6e6e6';
            ctx.fillRect(0, 0, w, h);

            const pw = compiled.pageWidth * sc, ph = compiled.pageHeight * sc;
            const ox = (w - pw) / 2 + panX, oy = (h - ph) / 2 + panY;

            ctx.fillStyle = '#fff';
            ctx.fillRect(ox, oy, pw, ph);
            ctx.strokeStyle = '#b4b4b4';
            ctx.strokeRect(ox, oy, pw, ph);

            for (const it of compiled.dlItems) {
                if (it.page !== curPage) continue;
                const mx = ox + it.x * sc;
                if (it.type === 'RECT' || it.type === 'ROUND_RECT') {
                    const my = oy + (compiled.pageHeight - (it.y + it.h)) * sc;
                    const mw = Math.max(1, it.w * sc), mh = Math.max(1, it.h * sc);
                    const hasFill = it.c && it.c.name !== 'none';
                    ctx.beginPath();
                    if (it.type === 'ROUND_RECT' && ctx.roundRect) ctx.roundRect(mx, my, mw, mh, it.radius * sc);
                    else ctx.rect(mx, my, mw, mh);
                    if (hasFill) { ctx.fillStyle = `rgb(${it.c.r},${it.c.g},${it.c.b})`; ctx.fill(); }
                    if (it.strokeW > 0) { ctx.strokeStyle = `rgb(${it.strokeC.r},${it.strokeC.g},${it.strokeC.b})`; ctx.lineWidth = Math.max(1, it.strokeW * sc); ctx.stroke(); }
                } else if (it.type === 'LINE') {
                    const my1 = oy + (compiled.pageHeight - it.y) * sc;
                    const my2 = oy + (compiled.pageHeight - (it.y + it.h)) * sc;
                    ctx.beginPath();
                    ctx.moveTo(mx, my1); ctx.lineTo(mx + it.w * sc, my2);
                    ctx.strokeStyle = `rgb(${it.c.r},${it.c.g},${it.c.b})`;
                    ctx.lineWidth = Math.max(1, it.strokeW * sc);
                    ctx.stroke();
                } else if (it.type === 'TEXT') {
                    const my = oy + (compiled.pageHeight - it.y) * sc;
                    ctx.save();
                    ctx.translate(mx, my);
                    if (it.angle) ctx.rotate(-it.angle * Math.PI / 180);
                    ctx.font = `${it.isItalic ? 'italic ' : ''}${it.isBold ? 'bold ' : ''}${Math.max(1, it.fontSize * sc)}px Helvetica, Arial, sans-serif`;
                    ctx.fillStyle = `rgb(${it.c.r},${it.c.g},${it.c.b})`;
                    ctx.fillText(it.text, 0, 0);
                    ctx.restore();
                } else if (it.type === 'PIE') {
                    const my = oy + (compiled.pageHeight - (it.y + it.h)) * sc;
                    const r = (it.w * sc) / 2, cx = mx + r, cy = my + r;
                    ctx.beginPath();
                    ctx.moveTo(cx, cy);
                    ctx.arc(cx, cy, r, it.startAngle, it.endAngle);
                    ctx.closePath();
                    ctx.fillStyle = `rgb(${it.c.r},${it.c.g},${it.c.b})`;
                    ctx.fill();
                    ctx.strokeStyle = '#fff'; ctx.lineWidth = 1.5; ctx.stroke();
                }
            }
        }

        const doSearch = (dir) => {
            const q = (srchIn.value || '').toLowerCase();
            if (!q || !compiled.dlItems.length) return;
            const n = compiled.dlItems.length;
            let start = (searchIdx + dir + n) % n;
            for (let i = 0; i < n; i++) {
                const idx = (start + i * dir + n) % n;
                const it = compiled.dlItems[idx];
                if (it.type === 'TEXT' && it.text.toLowerCase().includes(q)) {
                    searchIdx = idx; curPage = it.page; paint(); return;
                }
            }
        };

        host.querySelector('.tj-first').onclick = () => { curPage = 0; paint(); };
        host.querySelector('.tj-prev').onclick = () => { curPage = (curPage - 1 + compiled.numPages) % compiled.numPages; paint(); };
        host.querySelector('.tj-next').onclick = () => { curPage = (curPage + 1) % compiled.numPages; paint(); };
        host.querySelector('.tj-last').onclick = () => { curPage = compiled.numPages - 1; paint(); };
        host.querySelector('.tj-fitw').onclick = () => { zoomMode = 'FIT_WIDTH'; panX = panY = 0; paint(); };
        host.querySelector('.tj-fitp').onclick = () => { zoomMode = 'FIT_PAGE'; panX = panY = 0; paint(); };
        host.querySelector('.tj-zin').onclick = () => { customZoom = getScale() * 1.25; zoomMode = 'CUSTOM'; paint(); };
        host.querySelector('.tj-zout').onclick = () => { customZoom = getScale() * 0.8; zoomMode = 'CUSTOM'; paint(); };
        host.querySelector('.tj-srch-p').onclick = () => doSearch(-1);
        host.querySelector('.tj-srch-n').onclick = () => doSearch(1);
        srchIn.onkeydown = (e) => { if (e.key === 'Enter') doSearch(1); };
        host.querySelector('.tj-pdf').onclick = () => downloadPdf(typstSource, opts.filename || 'report.pdf');

        stage.onmousedown = (e) => { isPanning = true; dragX = e.clientX; dragY = e.clientY; stage.style.cursor = 'grabbing'; };
        window.addEventListener('mouseup', () => { isPanning = false; stage.style.cursor = 'grab'; });
        window.addEventListener('mousemove', (e) => {
            if (!isPanning) return;
            panX += e.clientX - dragX; panY += e.clientY - dragY;
            dragX = e.clientX; dragY = e.clientY; paint();
        });
        if (window.ResizeObserver) new ResizeObserver(paint).observe(stage);
        paint();
    }

    async function openReportPdf(reportId, mode = 'tab') {
        const res = await fetch('/api/report_typ?report_id=' + encodeURIComponent(reportId));
        if (!res.ok) throw new Error('Failed to load report Typst source');
        const typ = await res.text();
        if (mode === 'download') downloadPdf(typ, `report_${reportId}.pdf`);
        else openPdfInNewTab(typ);
    }

    return { compileDocument, compileToPdfBytes, compileToPdfBlob, downloadPdf, openPdfInNewTab, mountViewer, openReportPdf };
})();