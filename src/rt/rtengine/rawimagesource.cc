/* 
 */

/*

    Copyright (C) 2014 Ferrero Andrea

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.


 */

/*

    These files are distributed with PhotoFlow - http://aferrero2707.github.io/PhotoFlow/

 */

#include "rawimagesource.hh"

#define RT_EMU


    /**
    * @brief Get the gamma value for Gamma=2.4 Slope=17
    * @param x red, green or blue channel's value [0 ; 1]
    * @return the gamma modified's value [0 ; 1]
    */
    static inline double gamma24_17     (double x)
    {
        return x <= 0.001867 ? x * 17.0 : 1.044445 * exp(log(x) / 2.4) - 0.044445;
    }


    /**
    * @brief Get the inverse gamma value for Gamma=2.4 Slope=17
    * @param x red, green or blue channel's value [0 ; 1]
    * @return the inverse gamma modified's value [0 ; 1]
    */
    static inline double igamma24_17    (double x)
    {
        return x <= 0.031746 ? x / 17.0 : exp(log((x + 0.044445) / 1.044445) * 2.4);
    }




rtengine::RawImageSource::RawImageSource(): FC_roffset(0), FC_coffset(0), image_data(NULL)
{
  igammatab_24_17(65536, 0);
  gammatab_24_17a(65536, LUT_CLIP_ABOVE | LUT_CLIP_BELOW);
  for (int i = 0; i < 65536; i++) {
    float j = (float)i / 65535.0f;
    gammatab_24_17a[i] = gamma24_17(j);
  }

  for (int i = 0; i < 65536; i++) {
    igammatab_24_17[i] = (65535.0 * igamma24_17 (i / 65535.0));
  }


}



void rtengine::RawImageSource::ca_correct(VipsRegion* ir, VipsRegion* oreg, bool autoCA, float cared, float cablue)
{
  int x, y;
  int border = 8;
  int border2 = border*2;

  Rect *r = &oreg->valid;
  int raw_left = (r->left/2)*2;
  int raw_top = (r->top/2)*2;
  int raw_right = r->left+r->width-1;
  int raw_bottom = r->top+r->height-1;

  // Make sure the border is entirely processed
  //if( raw_left < border ) raw_left = 0;
  //if( raw_top < border ) raw_top = 0;
  //if( raw_right > (ir->im->Xsize-border-1) ) raw_right = ir->im->Xsize-1;
  //if( raw_bottom > (ir->im->Ysize-border-1) ) raw_bottom = ir->im->Ysize-1;

  // Portion of the image to be processed (pixels border is excluded)
  VipsRect r_img = {border, border, ir->im->Xsize-border2, ir->im->Ysize-border2};
  //std::cout<<"image: "<<ir->im->Xsize<<","<<ir->im->Ysize<<"+"<<0<<"+"<<0<<std::endl;
  //std::cout<<"r_img: "<<r_img.width<<","<<r_img.height<<"+"<<r_img.left<<"+"<<r_img.top<<std::endl;
  //VipsRect r_img = {0, 0, ir->im->Xsize, ir->im->Ysize};

  // Output region aligned to Bayer pattern and with pixels border excluded
  VipsRect r_raw = {raw_left, raw_top, raw_right-raw_left+1, raw_bottom-raw_top+1};
  if( (r_raw.width%2) ) r_raw.width += 1;
  if( (r_raw.height%2) ) r_raw.height += 1;
  //std::cout<<"r_raw(1): "<<r_raw.width<<","<<r_raw.height<<"+"<<r_raw.left<<"+"<<r_raw.top<<std::endl;
  vips_rect_intersectrect (&r_raw, &r_img, &r_raw);
  //std::cout<<"r_raw(2): "<<r_raw.width<<","<<r_raw.height<<"+"<<r_raw.left<<"+"<<r_raw.top<<std::endl;

#ifndef NDEBUG
  std::cout<<"RawImageSource::ca_correct(): rawData.init( "<<ir->valid.width<<", "<<ir->valid.height<<", "
           <<ir->valid.top<<", "<<ir->valid.left<<" )"<<std::endl;
#endif

  // Initialization of pixel matrices
  tile_top = ir->valid.top;
  tile_left = ir->valid.left;
  rawData.init( ir->valid.width, ir->valid.height, ir->valid.top, ir->valid.left );
  for( y = 0; y < ir->valid.height; y++ ) {
    PF::raw_pixel_t* ptr = ir ? (PF::raw_pixel_t*)VIPS_REGION_ADDR( ir, ir->valid.left, y+ir->valid.top ) : NULL;
    rawData.set_row( y+ir->valid.top, ptr );
  }
  //red.Init( r_raw.width, r_raw.height, r_raw.top, r_raw.left );
  //green.Init( r_raw.width, r_raw.height, r_raw.top, r_raw.left );
  //blue.Init( r_raw.width, r_raw.height, r_raw.top, r_raw.left );

  // Call to demosaicing method
  CA_correct_RT(0, 0, ir->im->Xsize, ir->im->Ysize,
                    r_raw.left, r_raw.top, r_raw.width, r_raw.height,
                    autoCA, cared, cablue);

  VipsRect r_out;
  vips_rect_intersectrect (r, &r_img, &r_out);

  int xx;
  for( y = 0; y < r_out.height; y++ ) {
    //float* ptr = (float*)VIPS_REGION_ADDR( oreg, r_out.left, y+r_out.top );
    PF::raw_pixel_t* ptr = (PF::raw_pixel_t*)VIPS_REGION_ADDR( oreg, r_out.left, y+r_out.top );
    for( x = 0; x < r_out.width; x++ ) {
      ptr[x][0] = rawData[y+r_out.top][x+r_out.left];
      ptr[x][1] = rawData[y+r_out.top].color(x+r_out.left);
/*
#ifdef RT_EMU
      // RawTherapee emulation
      ptr[x*3] = CLAMP( red[y+r_out.top][x+r_out.left]/65535, 0, 95 );
      ptr[x*3+1] = CLAMP( green[y+r_out.top][x+r_out.left]/65535, 0, 95 );
      ptr[x*3+2] = CLAMP( blue[y+r_out.top][x+r_out.left]/65535, 0, 95 );
#else
      ptr[xx] = CLAMP( red[y+r_out.top][x+r_out.left], 0, 1 );
      ptr[xx+1] = CLAMP( green[y+r_out.top][x+r_out.left], 0, 1 );
      ptr[xx+2] = CLAMP( blue[y+r_out.top][x+r_out.left], 0, 1 );
#endif
*/
    }
  }
}



void rtengine::RawImageSource::amaze_demosaic(VipsRegion* ir, VipsRegion* oreg)
{
	int x, y;
  int border = 16;

  Rect *r = &oreg->valid;
	int raw_left = (r->left/2)*2;
	int raw_top = (r->top/2)*2;
	int raw_right = r->left+r->width-1;
	int raw_bottom = r->top+r->height-1;

	// Make sure the border is entirely processed
  //if( raw_left < border ) raw_left = 0;
  //if( raw_top < border ) raw_top = 0;
  //if( raw_right > (ir->im->Xsize-border-1) ) raw_right = ir->im->Xsize-1;
  //if( raw_bottom > (ir->im->Ysize-border-1) ) raw_bottom = ir->im->Ysize-1;

	// Portion of the image to be processed (a 16 pixels border is excluded)
  VipsRect r_img = {16, 16, ir->im->Xsize-32, ir->im->Ysize-32};
  //std::cout<<"image: "<<ir->im->Xsize<<","<<ir->im->Ysize<<"+"<<0<<"+"<<0<<std::endl;
  //std::cout<<"r_img: "<<r_img.width<<","<<r_img.height<<"+"<<r_img.left<<"+"<<r_img.top<<std::endl;
  //VipsRect r_img = {0, 0, ir->im->Xsize, ir->im->Ysize};

	// Output region aligned to Bayer pattern and with 16 pixels border excluded
  VipsRect r_raw = {raw_left, raw_top, raw_right-raw_left+1, raw_bottom-raw_top+1};
  if( (r_raw.width%2) ) r_raw.width += 1;
  if( (r_raw.height%2) ) r_raw.height += 1;
  //std::cout<<"r_raw(1): "<<r_raw.width<<","<<r_raw.height<<"+"<<r_raw.left<<"+"<<r_raw.top<<std::endl;
  vips_rect_intersectrect (&r_raw, &r_img, &r_raw);
  //std::cout<<"r_raw(2): "<<r_raw.width<<","<<r_raw.height<<"+"<<r_raw.left<<"+"<<r_raw.top<<std::endl;

#ifndef NDEBUG
	std::cout<<"rawData.init( "<<ir->valid.width<<", "<<ir->valid.height<<", "
					 <<ir->valid.top<<", "<<ir->valid.left<<" )"<<std::endl;
#endif

	// Initialization of pixel matrices
	tile_top = ir->valid.top;
	tile_left = ir->valid.left;
  rawData.init( ir->valid.width, ir->valid.height, ir->valid.top, ir->valid.left );
  for( y = 0; y < ir->valid.height; y++ ) {
    PF::raw_pixel_t* ptr = ir ? (PF::raw_pixel_t*)VIPS_REGION_ADDR( ir, ir->valid.left, y+ir->valid.top ) : NULL; 
    rawData.set_row( y+ir->valid.top, ptr );
  }
  red.Init( r_raw.width, r_raw.height, r_raw.top, r_raw.left );
  green.Init( r_raw.width, r_raw.height, r_raw.top, r_raw.left );
  blue.Init( r_raw.width, r_raw.height, r_raw.top, r_raw.left );

	// Call to demosaicing method
  amaze_demosaic_RT(0, 0, ir->im->Xsize, ir->im->Ysize,
                    r_raw.left, r_raw.top, r_raw.width, r_raw.height);

  VipsRect r_out;
  vips_rect_intersectrect (r, &r_img, &r_out);

  int xx;
  for( y = 0; y < r_out.height; y++ ) {
    float* ptr = (float*)VIPS_REGION_ADDR( oreg, r_out.left, y+r_out.top ); 
    for( x = 0, xx = 0; x < r_out.width; x++, xx+=3 ) {
#ifdef RT_EMU
      /* RawTherapee emulation */
      ptr[x*3] = CLAMP( red[y+r_out.top][x+r_out.left]/65535, 0, 95 );
      ptr[x*3+1] = CLAMP( green[y+r_out.top][x+r_out.left]/65535, 0, 95 );
      ptr[x*3+2] = CLAMP( blue[y+r_out.top][x+r_out.left]/65535, 0, 95 );
#else
      ptr[xx] = CLAMP( red[y+r_out.top][x+r_out.left], 0, 1 );
      ptr[xx+1] = CLAMP( green[y+r_out.top][x+r_out.left], 0, 1 );
      ptr[xx+2] = CLAMP( blue[y+r_out.top][x+r_out.left], 0, 1 );
#endif
    }
  }
}



void rtengine::RawImageSource::lmmse_demosaic(VipsRegion* ir, VipsRegion* oreg)
{
  int x, y;
  int padding = 10;

  Rect *r = &oreg->valid;
  int raw_left = (r->left/2)*2;
  int raw_top = (r->top/2)*2;
  int raw_right = r->left+r->width-1;
  int raw_bottom = r->top+r->height-1;

  // Make sure the border is entirely processed
  //if( raw_left < border ) raw_left = 0;
  //if( raw_top < border ) raw_top = 0;
  //if( raw_right > (ir->im->Xsize-border-1) ) raw_right = ir->im->Xsize-1;
  //if( raw_bottom > (ir->im->Ysize-border-1) ) raw_bottom = ir->im->Ysize-1;

  // Portion of the image to be processed (a 10 pixels border is excluded)
  VipsRect r_img = {padding, padding, ir->im->Xsize-padding*2, ir->im->Ysize-padding*2};
  //std::cout<<"image: "<<ir->im->Xsize<<","<<ir->im->Ysize<<"+"<<0<<"+"<<0<<std::endl;
  //std::cout<<"r_img: "<<r_img.width<<","<<r_img.height<<"+"<<r_img.left<<"+"<<r_img.top<<std::endl;
  //VipsRect r_img = {0, 0, ir->im->Xsize, ir->im->Ysize};

  // Output region aligned to Bayer pattern and with 16 pixels border excluded
  VipsRect r_raw = {raw_left, raw_top, raw_right-raw_left+1, raw_bottom-raw_top+1};
  if( (r_raw.width%2) ) r_raw.width += 1;
  if( (r_raw.height%2) ) r_raw.height += 1;
  //std::cout<<"r_raw(1): "<<r_raw.width<<","<<r_raw.height<<"+"<<r_raw.left<<"+"<<r_raw.top<<std::endl;
  vips_rect_intersectrect (&r_raw, &r_img, &r_raw);
  //std::cout<<"r_raw(2): "<<r_raw.width<<","<<r_raw.height<<"+"<<r_raw.left<<"+"<<r_raw.top<<std::endl;

#ifndef NDEBUG
  std::cout<<"rawData.init( "<<ir->valid.width<<", "<<ir->valid.height<<", "
           <<ir->valid.top<<", "<<ir->valid.left<<" )"<<std::endl;
#endif

  // Initialization of pixel matrices
  tile_top = ir->valid.top;
  tile_left = ir->valid.left;
  rawData.init( ir->valid.width, ir->valid.height, ir->valid.top, ir->valid.left );
  for( y = 0; y < ir->valid.height; y++ ) {
    PF::raw_pixel_t* ptr = ir ? (PF::raw_pixel_t*)VIPS_REGION_ADDR( ir, ir->valid.left, y+ir->valid.top ) : NULL;
    rawData.set_row( y+ir->valid.top, ptr );
  }
  red.Init( r_raw.width, r_raw.height, r_raw.top, r_raw.left );
  green.Init( r_raw.width, r_raw.height, r_raw.top, r_raw.left );
  blue.Init( r_raw.width, r_raw.height, r_raw.top, r_raw.left );

  // Call to demosaicing method
  lmmse_demosaic_RT(0, 0, ir->im->Xsize, ir->im->Ysize,
                    r_raw.left, r_raw.top, r_raw.width, r_raw.height, 0);

  VipsRect r_out;
  vips_rect_intersectrect (r, &r_img, &r_out);

  int xx;
  for( y = 0; y < r_out.height; y++ ) {
    float* ptr = (float*)VIPS_REGION_ADDR( oreg, r_out.left, y+r_out.top );
    for( x = 0, xx = 0; x < r_out.width; x++, xx+=3 ) {
#ifdef RT_EMU
      /* RawTherapee emulation */
      ptr[x*3] = CLAMP( red[y+r_out.top][x+r_out.left]/65535, 0, 95 );
      ptr[x*3+1] = CLAMP( green[y+r_out.top][x+r_out.left]/65535, 0, 95 );
      ptr[x*3+2] = CLAMP( blue[y+r_out.top][x+r_out.left]/65535, 0, 95 );
#else
      ptr[xx] = CLAMP( red[y+r_out.top][x+r_out.left], 0, 1 );
      ptr[xx+1] = CLAMP( green[y+r_out.top][x+r_out.left], 0, 1 );
      ptr[xx+2] = CLAMP( blue[y+r_out.top][x+r_out.left], 0, 1 );
#endif
    }
  }
}



void rtengine::RawImageSource::igv_demosaic(VipsRegion* ir, VipsRegion* oreg)
{
	int x, y;

  Rect *r = &oreg->valid;

	// Portion of the image to be processed (a 16 pixels border is excluded)
  VipsRect r_img = {7, 7, ir->im->Xsize-14, ir->im->Ysize-14};

	// Initialization of pixel matrices
	tile_top = ir->valid.top;
	tile_left = ir->valid.left;
  rawData.init( ir->valid.width, ir->valid.height, ir->valid.top, ir->valid.left );
  for( y = 0; y < ir->valid.height; y++ ) {
    PF::raw_pixel_t* ptr = ir ? (PF::raw_pixel_t*)VIPS_REGION_ADDR( ir, ir->valid.left, y+ir->valid.top ) : NULL; 
    rawData.set_row( y+ir->valid.top, ptr );
  }
  red.Init( ir->valid.width, ir->valid.height, ir->valid.top, ir->valid.left );
  green.Init( ir->valid.width, ir->valid.height, ir->valid.top, ir->valid.left );
  blue.Init( ir->valid.width, ir->valid.height, ir->valid.top, ir->valid.left );

	// Call to demosaicing method
	igv_demosaic_RT(0, 0, ir->im->Xsize, ir->im->Ysize,
									ir->valid.left, ir->valid.top, ir->valid.width, ir->valid.height);

  VipsRect r_out;
  vips_rect_intersectrect (r, &r_img, &r_out);

  int xx;
  for( y = 0; y < r_out.height; y++ ) {
    float* ptr = (float*)VIPS_REGION_ADDR( oreg, r_out.left, y+r_out.top ); 
    for( x = 0, xx = 0; x < r_out.width; x++, xx+=3 ) {
#ifdef RT_EMU
      /* RawTherapee emulation */
      ptr[x*3] = red[y+r_out.top][x+r_out.left]/65535;
      ptr[x*3+1] = green[y+r_out.top][x+r_out.left]/65535;
      ptr[x*3+2] = blue[y+r_out.top][x+r_out.left]/65535;
#else
      ptr[xx] = CLAMP( red[y+r_out.top][x+r_out.left], 0, 1 );
      ptr[xx+1] = CLAMP( green[y+r_out.top][x+r_out.left], 0, 1 );
      ptr[xx+2] = CLAMP( blue[y+r_out.top][x+r_out.left], 0, 1 );
#endif
    }
  }
}




