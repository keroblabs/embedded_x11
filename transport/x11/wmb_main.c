/*
 * Reserved for WMB's portable lifecycle entry point.
 *
 * The old void main(void) stub was never a usable WMB entry point and
 * conflicted with the host's main().  The future implementation here
 * will define int wmb_main(const wmb_config_t *) and wmb_shutdown()
 * as specified by docs/90-decisions.md D-43.
 *
 * The host's actual main() belongs in linux_hosted/wmb_boot/, while
 * this module must stay independent of the hosting environment.
 *
 * Intentionally no symbols until the server and readiness gate exist.
 */
