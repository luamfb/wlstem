#ifndef WLSTEM_TRANSACTION_H_
#define WLSTEM_TRANSACTION_H_
#include <stdint.h>

/**
 * Transactions enable us to perform atomic layout updates.
 *
 * A transaction contains a list of windows and their new state.
 * A state might contain a new size, or new border settings, or new parent/child
 * relationships.
 *
 * Committing a transaction makes sway notify of all the affected clients with
 * their new sizes. We then wait for all the views to respond with their new
 * surface sizes. When all are ready, or when a timeout has passed, we apply the
 * updates all at the same time.
 *
 * When we want to make adjustments to the layout, we change the pending state
 * in windows, mark them as dirty and call transaction_commit_dirty(). This
 * create and commits a transaction from the dirty windows.
 */

struct sway_transaction_instruction;
struct sway_view;

/**
 * Find all dirty windows, create and commit a transaction containing them,
 * and unmark them as dirty.
 */
void transaction_commit_dirty(void);

/**
 * Notify the transaction system that a view is ready for the new layout.
 *
 * When all views in the transaction are ready, the layout will be applied.
 */
void transaction_notify_view_ready_by_serial(struct sway_view *view,
        uint32_t serial);

/**
 * Notify the transaction system that a view is ready for the new layout, but
 * identifying the instruction by geometry rather than by serial.
 *
 * This is used by xwayland views, as they don't have serials.
 */
void transaction_notify_view_ready_by_geometry(struct sway_view *view,
        double x, double y, int width, int height);

/**
 * Unconditionally notify the transaction system that a view is ready for the
 * new layout.
 */
void transaction_notify_view_ready_immediately(struct sway_view *view);

#endif
