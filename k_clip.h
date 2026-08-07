/* Bridge between the KolibriOS clipboard (system function 54) and the X11
 * CLIPBOARD selection, so text can be copied between KolibriOS apps running
 * under kex and the applications on the host desktop. */
#ifndef K_CLIP_H
#define K_CLIP_H

/* Take ownership of the X CLIPBOARD selection and publish `buf` (a KolibriOS
 * clipboard block: size, type, encoding, data). Non-text blocks are ignored. */
void k_clip_publish(const void* buf, unsigned size);

/* If some other X client owns the CLIPBOARD and its text differs from our
 * newest slot, pull it in as a new slot. Call before reporting the slot
 * count, so KolibriOS apps see host copies as ordinary clipboard entries. */
void k_clip_sync(void);

/* Handle SelectionRequest / SelectionClear. Returns 1 if the event was ours.
 * Declared void* so callers do not need Xlib in scope. */
int k_clip_event(void* xevent);

#endif /* K_CLIP_H */
