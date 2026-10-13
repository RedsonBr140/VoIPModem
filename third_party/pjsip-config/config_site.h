/* VoIPModem build configuration for pjproject.
 *
 * pjproject's build requires <pj/config_site.h> to exist, but
 * intentionally does not track it in git (upstream's own CI copies
 * config_site_test.h into place before building — see
 * third_party/pjproject/CLAUDE.md). We do the same thing: this is the
 * canonical, VoIPModem-tracked copy; scripts/setup-pjproject.ps1 copies
 * it into third_party/pjproject/pjlib/include/pj/config_site.h before
 * building. Do not edit the copy inside third_party/pjproject directly —
 * it is untracked there and will be overwritten/lost on re-setup.
 *
 * Kept minimal: VoIPModem only needs G.711 (PCMU/PCMA), which pjmedia
 * builds in by default, so there is nothing to enable here. No SSL/TLS
 * is configured since the existing Asterisk infrastructure is reached
 * over the local/internal network, not the public Internet.
 */

/* Suppress "label 'TODO*' defined but not used" from PJ_TODO() macros
 * scattered through pjproject's own source; harmless but noisy.
 */
#define PJ_TODO(x)
