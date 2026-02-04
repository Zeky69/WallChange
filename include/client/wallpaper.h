#ifndef WALLPAPER_H
#define WALLPAPER_H

int download_image(const char *url, const char *filepath);
void set_wallpaper(const char *filepath);
void apply_wallpaper_effect(const char *filepath, const char *effect, int value);

#endif
