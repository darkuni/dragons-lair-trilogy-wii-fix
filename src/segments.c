/* The 32 Space Ace energize segments that the Wii release re-graded blue, plus the developers' original
 * red SAScene1 that ships unused on the same disc. Frame ranges are inclusive; every range starts on a
 * keyframe and the frame after it is a keyframe. SHA-1s identify the untouched retail files.
 * clamp: palette clamp upper hue (deg) or -1. maxref/intra: encoder profile (see mobienc.h). */
#include <stddef.h>
#include "segments.h"
const dlt_segment dlt_segments[] = {
 /* name          first last clamp maxref intra  size      sha1 of retail file */
 {"SAScene1",      359, 431, -1, 0, NULL,  24725168, "e1900abd54c42bca0b84e2ccf5efe1fa7c1d1ddf"},  /* replaced by SAScene1_original */
 {"SAScene1W",     360, 431, -1, 5, NULL,  25006608, "9ec3eec85f6e8d74d44c1cdfa84b4a3385d85280"},
 {"SAScene4",      256, 365, 25, 1, "013", 20118640, "21c058bae09b8dafa0f67bc9d2e7660e3762133c"},
 {"SAScene4W",     256, 365, 25, 1, "013", 20294836, "05c26b7bb5482c6aeff55a0fa3143623a706e94f"},
 {"SAScene5",      256, 365, 25, 1, "013", 20112464, "bda7fe5621416c546adef82df19216f6e34b9f52"},
 {"SAScene5W",     256, 365, 25, 1, "013", 20273012, "cf791a883b07a9ace8c7932c652e6a69d0483cc2"},
 {"SAScene6",      260, 299, -1, 5, NULL,  18872816, "7b7287e9735f4cf395c152f32afd50bca3c9caf7"},
 {"SAScene6W",     260, 299, -1, 5, NULL,  18151980, "e2bafe36a2332ff03834cd5187ff1a090a113cd2"},
 {"SAScene7",      260, 299, -1, 5, NULL,  19095504, "119a74ee3929a83a3557c66001309e6c7917fb02"},
 {"SAScene7W",     260, 299, -1, 5, NULL,  19000920, "8f5cb5212630ef37b909c95cfba8924a6d54ed09"},
 {"SAScene8",      216, 239, -1, 5, NULL,  17000716, "73b6b2f19bbb45479b7c513d681cdd00b966eb34"},
 {"SAScene8W",     216, 239, -1, 5, NULL,  16990060, "64ae52c9d55db0c53da5cee5ba429660ea9e7d90"},
 {"SAScene9",      184, 239, -1, 5, NULL,  16915492, "c78ff260c2a8b67ddffc86d3c3d11a1e118ecdf9"},
 {"SAScene9W",     184, 238, -1, 5, NULL,  16898936, "c61e802dafb1048d565c4066c947db960a6e9a4c"},
 {"SAScene16",     184, 223, -1, 5, NULL,   9566728, "5c29c0ca7cfd1a974ade1fa9337aa01cb7b5bf0b"},
 {"SAScene16W",    184, 223, -1, 5, NULL,   9497912, "431adcc2e7b575dcbc5b30e948a57b8654c7c090"},
 {"SAScene17",     184, 223, -1, 5, NULL,   9555976, "970eb8072558a0d5ab65d3971e1795b6ea727585"},
 {"SAScene17W",    184, 223, -1, 5, NULL,   9508572, "9348374214d315d452bc5c379f9286361b8ec965"},
 {"SAScene18",     208, 259, -1, 5, NULL,  16129292, "376b6d742d818fbeec7232b4b1b9efd8bb82957f"},
 {"SAScene18W",    208, 259, -1, 5, NULL,  16135804, "ebbf46cd3aed20712700032622c2fe76711d0bd8"},
 {"SAScene19",     208, 259, -1, 5, NULL,  16096616, "ae7a3d1847b0c3c68b37745463fddee8f7a133a3"},
 {"SAScene19W",    208, 259, -1, 5, NULL,  16093300, "56274c6c2648e6da41c6850c5e45925d48fb6589"},
 {"SAScene20",     290, 351, -1, 5, NULL,  26972748, "6c0d80de275c9cef9a620c83258b9294068e9d04"},
 {"SAScene20W",    290, 351, -1, 5, NULL,  26884208, "58cd01cd3bac3f5515a82d2c6f242b466d9f7f75"},
 {"SAScene21",     290, 351, -1, 5, NULL,  26935276, "12db95806ea98f36e9782b412d805d747ef7d241"},
 {"SAScene21W",    290, 351, -1, 5, NULL,  26921508, "4f22671e88abb3720c6d0a94b6af7d6ef040ea77"},
 {"SAScene22",     168, 208, -1, 5, NULL,  24471492, "1eea5d7592f454d9cd0e53afe23798442846ab11"},
 {"SAScene22W",    168, 208, -1, 5, NULL,  24721408, "babf769f7cd0cef1e4e559f0b7290fe317437e15"},
 {"SAScene23",     168, 208, -1, 5, NULL,  24477624, "7233fc6d980c75f4885f99f5b33b5dbec29aac09"},
 {"SAScene23W",    168, 208, -1, 5, NULL,  24736704, "270eaa41862aeefd1dc804f6d8b1755b6ca3bd63"},
 {"SAScene24",     448, 470, -1, 5, NULL,  45596036, "7df517ad83cf6ed4a01ac4ccbc04140871c0b867"},
 {"SAScene24W",    448, 471, -1, 5, NULL,  45774248, "e717c4e41789b9d0662c562557acf797305f6dc0"},
};
const int dlt_nsegments = (int)(sizeof dlt_segments / sizeof dlt_segments[0]);
const dlt_segment dlt_sa1_original = {"SAScene1_original", 0, 0, -1, 0, NULL, 24732180, "7c51a5b815f516ec0fba0c1fcf53545b314b3ac8"};
