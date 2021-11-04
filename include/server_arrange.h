#ifndef SERVER_ARRANGE_H_
#define SERVER_ARRANGE_H_

struct sway_output;
struct wls_window;

void arrange_window(struct wls_window *window);

void arrange_output(struct sway_output *output);

void arrange_root(void);

void arrange_output_layout(void);

void arrange_layers(struct sway_output *output);

#endif /* SERVER_ARRANGE_H_ */
