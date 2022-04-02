#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <immintrin.h>
#include <assert.h>
#include <stdint.h>

const char WINDOW_TITLE[] = "Alpha blending"; 

static SDL_Surface* BlendAVX(SDL_Surface *back, SDL_Surface *front, int x = 0, int y = 0)
{
        assert(back);
        assert(front);

        SDL_LockSurface(back);

        printf("back: %s\n",  SDL_GetPixelFormatName(back->format->format));
        printf("front: %s\n", SDL_GetPixelFormatName(front->format->format));

        const __m256i xff = _mm256_set1_epi16(0xff);
        
        for (int h = 0; h < front->h; h++)
                for (int w = 0; w < front->w; w += 4) {

                        __m256i fg = _mm256_cvtepu8_epi16(
                                _mm_load_si128((__m128i *)((uint32_t *)front->pixels + front->w * h + w))
                        );

                        __m256i bg = _mm256_cvtepu8_epi16(
                                _mm_load_si128((__m128i *)((uint32_t *) back->pixels +  back->w * (h + y) + (w + x)))
                        );

                        const __m256i alpha_mask = _mm256_set_epi8(
                                0xff, 0x0e, 0xff, 0x0e, 0xff, 0x0e, 0xff, 0x0e,
                                0xff, 0x06, 0xff, 0x06, 0xff, 0x06, 0xff, 0x06,
                                0xff, 0x0e, 0xff, 0x0e, 0xff, 0x0e, 0xff, 0x0e,
                                0xff, 0x06, 0xff, 0x06, 0xff, 0x06, 0xff, 0x06
                        );

                        __m256i alpha = _mm256_shuffle_epi8(fg, alpha_mask);

                        fg = _mm256_mullo_epi16(fg, alpha); 
                        bg = _mm256_mullo_epi16(bg, _mm256_sub_epi16(xff, alpha));

                        __m256i sum = _mm256_add_epi16(fg, bg);                        

                        const __m256i mask = _mm256_set_epi8(
                                0x0f, 0x0d, 0x0b, 0x09, 0x07, 0x05, 0x03, 0x01,
                                0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                0x0f, 0x0d, 0x0b, 0x09, 0x07, 0x05, 0x03, 0x01
                        ); 

                        sum = _mm256_shuffle_epi8(sum, mask);
                        __m128i color = _mm_add_epi64(
                                _mm256_extractf128_si256(sum, 1), _mm256_castsi256_si128(sum)
                        );

                        _mm_store_si128((__m128i *)((uint32_t *)back->pixels + back->w * (h + y) + (w + x)), color); 
                }

        SDL_UnlockSurface(back);
        return back;
}

static SDL_Surface* Blend(SDL_Surface *back, SDL_Surface *front, int x = 0, int y = 0)
{
        assert(back);
        assert(front);

        SDL_LockSurface(back);

        printf("back: %s\n", SDL_GetPixelFormatName(back->format->format));
        printf("front: %s\n", SDL_GetPixelFormatName(front->format->format));
        
        for (int h = 0; h < front->h; h++)
                for (int w = 0; w < front->w; w++) {
                        SDL_Color *fgc = (SDL_Color *)front->pixels + front->w * h + w;
                        SDL_Color *bgc = (SDL_Color *)back->pixels + back->w * (h + y) + (w + x);

                        *((SDL_Color *)back->pixels + back->w * (h + y) + (w + x)) = {
                                (Uint8)((fgc->r * fgc->a + bgc->r * (0xff - fgc->a)) >> 8),
                                (Uint8)((fgc->g * fgc->a + bgc->g * (0xff - fgc->a)) >> 8),
                                (Uint8)((fgc->b * fgc->a + bgc->b * (0xff - fgc->a)) >> 8),
                                0xff
                        }; 
                }

        SDL_UnlockSurface(back);
        return back;
}


static SDL_Surface *LoadBMP(const char *file)
{
        assert(file);

        SDL_Surface *img = SDL_LoadBMP(file);
        if (!img)
                printf("Failed to load image at %s: %s\n", file, SDL_GetError());
                
        return img;
}

int main(int argc, char *argv[])
{
        if (argc != 3) {
                fprintf(stderr, "Invalid arguments number");
                return EXIT_FAILURE;
        }

        SDL_Surface *background = LoadBMP(argv[1]);
        if (!background)
                return EXIT_FAILURE;

        SDL_Surface *fg = LoadBMP(argv[2]);
        if (!fg) {
                SDL_FreeSurface(background);
                return EXIT_FAILURE;  
        }

        SDL_Surface *foreground = SDL_CreateRGBSurfaceWithFormat(
                0, fg->w, fg->h, 32, SDL_PIXELFORMAT_ARGB8888
        );
        
        SDL_BlitSurface(fg, nullptr, foreground, nullptr);
        SDL_FreeSurface(fg);
        
        int error = SDL_Init(SDL_INIT_VIDEO);
        if (error) {
                fprintf(stderr, "Unable to initialize SDL:  %s\n", SDL_GetError());
                SDL_FreeSurface(foreground);
                SDL_FreeSurface(background);
                return EXIT_FAILURE;
        }

        atexit(SDL_Quit);
        
        SDL_Window *window = SDL_CreateWindow(
                WINDOW_TITLE, 
                SDL_WINDOWPOS_UNDEFINED,
                SDL_WINDOWPOS_UNDEFINED,
                background->w,
                background->h,
                SDL_WINDOW_SHOWN
        );
        
        if (!window) {
                fprintf(stderr, "Could not create window: %s\n", SDL_GetError());
                SDL_FreeSurface(foreground);
                SDL_FreeSurface(background);
                return EXIT_FAILURE;
        }
        
        SDL_Surface *surface = SDL_GetWindowSurface(window);

        SDL_Delay(100);

        SDL_BlitSurface(background, nullptr, surface, nullptr);
        SDL_FreeSurface(background);
        
        BlendAVX(surface, foreground, 300, 200);  
        
        SDL_Event event;
        float elapsed = 0.0f;
        for (int active = SDL_TRUE; active;) {
                Uint64 startf = SDL_GetPerformanceCounter();
        
                while (SDL_PollEvent(&event)) {
                        switch(event.type) {
                        case SDL_QUIT: active = SDL_FALSE; break;
                        default: break;
                        }
                }
                                      
                SDL_UpdateWindowSurface(window);

                Uint64 endf = SDL_GetPerformanceCounter();
                elapsed = (endf - startf) / (float)SDL_GetPerformanceFrequency();
                //fprintf(stderr, "FPS: %f\n", 1 / elapsed);
        }

        SDL_FreeSurface(foreground);
        SDL_DestroyWindow(window);
        
        return EXIT_SUCCESS;
}
