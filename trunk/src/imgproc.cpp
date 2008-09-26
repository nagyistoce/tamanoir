/***************************************************************************
 *           imgproc.cpp - Main image processing 
 *
 *  Tue Oct 23 22:10:56 2007
 *  Copyright  2007  Christophe Seyve 
 *  Email cseyve@free.fr
 ****************************************************************************/

/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <highgui.h>

#include "imgproc.h"
#include "imgutils.h"

extern FILE * logfile;
u8 g_debug_imgverbose = 1;
u8 g_debug_imgoutput = 0;
extern u8 g_debug_correlation;


/// Minimal dust size in pixel^2
#define DUST_MIN_SIZE		5

/// Minimal hot pixel size in pixel^2
#define HOTPIXEL_MIN_SIZE	1

#define TRUST_CORREL_MAX 4
#define TRUST_AREA_MAX	25


/** global option : stop if a dust has no replace candidate */
u8 g_option_stopoguess = 0;


TamanoirImgProc::TamanoirImgProc() {
	fprintf(logfile, "TamanoirImgProc::%s:%d ...\n", 
		__func__, __LINE__);
	init();
}

void TamanoirImgProc::init() {

	m_filename[0] = '\0';
	
	m_lock = false;
	
	
	m_FilmType = FILM_UNDEFINED;
	m_hotPixels = false;
	
	/* Default values : 
	- Trust good correction proposals : no
	- DPI : 2400 dpi
	- hot pixels : no (scan from film !)
	*/
	m_trust = false;
	m_dpi = 2400;
	m_hotPixels = false;
	
	/** Original image size */
	originalSize = cvSize(0,0);

	/** Processed imge size */
	processingSize = cvSize(0,0);

	// Image buffers
	originalImage = NULL;
	origBlurredImage = NULL;
	
	// Working images : full size 
	grayImage = NULL;
	medianImage = NULL;
	diffImage = NULL;
	growImage = NULL;
	
	// Working images : cropped
	cropImage = NULL;
	dilateImage = NULL;
	correctImage = NULL;
	tmpCropImage = NULL;
	cropColorImage = NULL;
	correctColorImage = NULL;
	sobelImage = NULL;
	
	// Display images
	displayImage = NULL;
	disp_cropColorImage =
		disp_correctColorImage =
		disp_dilateImage = 
		disp_cropImage = NULL;
	
	
	
	displaySize = cvSize(0,0);;
	
	
	memset(&m_correct, 0, sizeof(t_correction));
	memset(&m_last_correction, 0, sizeof(t_correction));
	
	/** Clear stats */
	memset(&m_dust_stats, 0, sizeof(dust_stats_t));
}

TamanoirImgProc::~TamanoirImgProc() {
	purge();
}



void TamanoirImgProc::purge() {
	if(originalImage) cvReleaseImage(&originalImage);  originalImage = NULL;
	if(displayImage) cvReleaseImage(&displayImage); displayImage = NULL;
	
	// Big images
	if(grayImage) cvReleaseImage(&grayImage);  grayImage = NULL;
	if(origBlurredImage) cvReleaseImage(&origBlurredImage); origBlurredImage = NULL;
	if(medianImage) cvReleaseImage(&medianImage);  medianImage = NULL;
	if(diffImage) 	cvReleaseImage(&diffImage);  diffImage = NULL;
	if(growImage) 	cvReleaseImage(&growImage);  growImage = NULL;
	
	// Copped images
	if(cropImage) cvReleaseImage(&cropImage);  cropImage = NULL;
	if(cropColorImage)	cvReleaseImage(&cropColorImage); cropColorImage = NULL;
	if(dilateImage) 	cvReleaseImage(&dilateImage);  		dilateImage = NULL;
	if(correctImage) 	cvReleaseImage(&correctImage);  	correctImage = NULL;
	if(tmpCropImage) 	cvReleaseImage(&tmpCropImage);  	tmpCropImage = NULL;
	if(sobelImage) 		cvReleaseImage(&sobelImage);  	sobelImage = NULL;
	
	
	// Display images
	if(disp_cropImage) cvReleaseImage(&disp_cropImage);  disp_cropImage = NULL;
	if(disp_cropColorImage) 	cvReleaseImage(&disp_cropColorImage); disp_cropColorImage = NULL;
	if(disp_correctColorImage) 	cvReleaseImage(&disp_correctColorImage); disp_correctColorImage = NULL;
	if(disp_dilateImage) 	cvReleaseImage(&disp_dilateImage); 	disp_dilateImage = NULL;
	
}




void TamanoirImgProc::setDisplaySize(int w, int h) {
	if(!medianImage) {
		displaySize = cvSize(w, h);
		return;
	}
	if(displayImage) 
		return; // Already displayed
	
	
	// Get best fit w/h for display in main frame
	int gray_width = grayImage->width;
	while((gray_width % 4) > 0)
		gray_width--;
	
	//cvReleaseImage(&displayImage); displayImage = NULL;
	
	int scaled_width = w;
	int scaled_height = h;
	
	if(grayImage->width > grayImage->height)
		scaled_height = scaled_width * grayImage->height/ grayImage->width;
	else
		scaled_width = scaled_height * grayImage->width / grayImage->height;
	
	
	// Use only size with 4xN dimensions
	while(scaled_width % 4) { scaled_width--; }
	while(scaled_height % 4) { scaled_height--; }
	
	displaySize = cvSize(scaled_width, scaled_height);
	
	// Resize
	displayImage = cvCreateImage(displaySize, IPL_DEPTH_8U, 1);
	fprintf(logfile, "TamanoirImgProc::%s:%d scaling %dx%d -> %dx%d...\n",
		__func__, __LINE__,
		grayImage->width, grayImage->height,
		displayImage->width, displayImage->height
		);
	
	
	cvResize(grayImage, displayImage, CV_INTER_LINEAR );
	
	
	// Prevent image values to be > 253
	for(int r=0; r<displayImage->height; r++)
	{
		u8 * lineGray = (u8 *)displayImage->imageData 
			+ r * displayImage->widthStep;
		for(int c = 0 ; c<displayImage->width; c++)
			if(lineGray[c] >= COLORMARK_FAILED)
				lineGray[c] = COLORMARK_FAILED-1;
	}
	
	if(g_debug_savetmp)  tmSaveImage(TMP_DIRECTORY "displayImage" IMG_EXTENSION, displayImage);
}




int TamanoirImgProc::loadFile(const char * filename) {
	
	int retry = 0;
	while(retry < 10 && m_lock) {
		sleep(1); retry++;
		fprintf(stderr, "[imgproc]::%s:%d : locked !!\n", __func__, __LINE__);
	}
	
	if(m_lock)
		return -1;
	
	m_lock = true;
	
	m_progress = 0;
	
	// Clear display and processing images
	purge();
	
	/* Load with OpenCV cvLoadImage 
	IplImage* cvLoadImage( const char* filename, int iscolor CV_DEFAULT(1));
	*/
	fprintf(logfile, "TamanoirImgProc::%s:%d : loading '%s'...\n", 
		__func__, __LINE__, filename);
	strcpy(m_filename, filename);
	
	originalImage = cvLoadImage(filename,
					(CV_LOAD_IMAGE_ANYDEPTH | CV_LOAD_IMAGE_ANYCOLOR)
					);
	m_progress = 10;

	if(!originalImage) {
		fprintf(logfile, "TamanoirImgProc::%s:%d : cannot open file '%s' !!\n",
					__func__, __LINE__, filename);
		m_lock = false;
		return -1;
	}
	
	switch(originalImage->depth) {
	case IPL_DEPTH_8S: 
		fprintf(logfile, "[imgutils] %s:%d : depth IPL_DEPTH_8S\n", __func__, __LINE__);
		break;
	case IPL_DEPTH_16S: 
		fprintf(logfile, "[imgutils] %s:%d : depth IPL_DEPTH_16S\n", __func__, __LINE__);
		break;
	case IPL_DEPTH_32S: 
		fprintf(logfile, "[imgutils] %s:%d : depth IPL_DEPTH_32S\n", __func__, __LINE__);
		break;
	case IPL_DEPTH_32F: 
		fprintf(logfile, "[imgutils] %s:%d : depth IPL_DEPTH_32F\n", __func__, __LINE__);
		break;
	case IPL_DEPTH_64F: 
		fprintf(logfile, "[imgutils] %s:%d : depth IPL_DEPTH_64F \n", __func__, __LINE__);
		break;	
	case IPL_DEPTH_8U: 
		fprintf(logfile, "[imgutils] %s:%d : depth IPL_DEPTH_8U \n", __func__, __LINE__);
		break;	
	case IPL_DEPTH_16U: 
		fprintf(logfile, "[imgutils] %s:%d : depth IPL_DEPTH_16U \n", __func__, __LINE__);
		break;	
	}
	
	
	fprintf(logfile, "TamanoirImgProc::%s:%d : '%s' => w=%d x h=%d x depth=%d x %d bytes\n", 
		__func__, __LINE__, filename, 
		originalImage->width, originalImage->height, originalImage->nChannels,
		tmByteDepth(originalImage));
	

	originalImage = tmAddBorder4x(originalImage);
	m_progress = 15;
	fprintf(logfile, "TamanoirImgProc::%s:%d : '%s' => w=%d x h=%d x channels=%d => %d bytes per pixel\n", 
			__func__, __LINE__, filename, 
			originalImage->width, originalImage->height, originalImage->nChannels,
			tmByteDepth(originalImage));
	
	// convert to Grayscaled image
	
	
	
	#ifdef CV_LOAD_IMAGE_GRAYSCALE
	fprintf(logfile, "TamanoirImgProc::%s:%d : reload as grayscaled image...\n", 
		__func__, __LINE__);
	if(originalImage->depth != IPL_DEPTH_8U) {
		grayImage = cvLoadImage(filename, CV_LOAD_IMAGE_GRAYSCALE);
		m_progress = 20;
		grayImage = tmAddBorder4x(grayImage);
		m_progress = 25;
	}
	else
	#endif
	{
	m_progress = 20;
	
	// convert full image to grayscale
	grayImage = cvCreateImage(cvSize(originalImage->width, originalImage->height), IPL_DEPTH_8U, 1);
	switch(originalImage->nChannels) {
	default:
	case 1:
		fprintf(logfile, "TamanoirImgProc::%s:%d : copy to Grayscaled image...\n", 
			__func__, __LINE__);
		memcpy(grayImage->imageData, originalImage->imageData, originalImage->widthStep * originalImage->height);
		break;
	case 3:
		fprintf(logfile, "TamanoirImgProc::%s:%d : convert to Grayscaled image...\n", 
			__func__, __LINE__);
		cvCvtColor(originalImage, grayImage, CV_RGB2GRAY);
		break;
	case 4:
		fprintf(logfile, "TamanoirImgProc::%s:%d : convert to Grayscaled image...\n", 
			__func__, __LINE__);
		cvCvtColor(originalImage, grayImage, CV_BGRA2GRAY);
		break;
	}
	}
	m_progress = 25;
	
	if(displaySize.width > 0)
		setDisplaySize(displaySize.width, displaySize.height);
	
	m_lock = false;
	
	float mean = 0.f, variance = 0.f, diff_mean = 0.f;
	int uniform = findUniform(&mean, &diff_mean, &variance);
	if(uniform >= 0) {
		fprintf(logfile, "TamanoirImgProc::%s:%d : Process uniform zone detection => mean=%g, diff=%g, var=%g\n",
			__func__, __LINE__, mean, diff_mean, variance);
	}
	
	fprintf(logfile, "TamanoirImgProc::%s:%d : pre-processing image...\n", 
		__func__, __LINE__);
	preProcessImage();
	
	return 0;
}

/*
 * IMAGE PRE-PROCESSING => DUST DETECTION MAP
 */
int TamanoirImgProc::preProcessImage() {
	
	int retry = 0;
	while(retry < 10 && m_lock) {
		sleep(1); retry++;
		fprintf(stderr, "[imgproc]::%s:%d : locked !!\n", __func__, __LINE__);
	}
	if(m_lock)
		return -1;
	
	m_lock = true;
	
	
	memset(&m_last_correction, 0, sizeof(t_correction));
	memset(&m_correct, 0, sizeof(t_correction));
	m_seed_x = m_seed_y = 0; // Reset search position
	
	m_progress = 25;
	originalSize = cvSize(originalImage->width, originalImage->height);
	
	// Blurred grayscaled image (Gaussian blur) 
	if(!medianImage) 
		medianImage = cvCreateImage(cvSize(originalImage->width, originalImage->height),
			IPL_DEPTH_8U, 1);
	
	// Difference (allocated here because it may be used for Open filter 
	if(!diffImage)
		diffImage = cvCreateImage(cvSize(originalImage->width, originalImage->height),
			IPL_DEPTH_8U, 1);
	
	m_progress = 30;
	// Smooth siz depend on DPI - size of 9 is ok at 2400 dpi
	m_smooth_size = 1 + 2*(int)(4 * m_dpi / 2400);
	if(m_smooth_size < 3)
		m_smooth_size = 3;
	
	fprintf(logfile, "TamanoirImgProc::%s:%d : blur grayscaled image ... size %d x %d\n", 
		__func__, __LINE__, m_smooth_size, m_smooth_size);

	switch(m_FilmType) {
	default: {
		fprintf(logfile, "TamanoirImgProc::%s:%d : smoothing input image as medianImage...\n", 
			__func__, __LINE__);
		
		cvSmooth(grayImage, medianImage, 
			CV_GAUSSIAN, //int smoothtype=CV_GAUSSIAN,
			m_smooth_size, m_smooth_size );
		
		}break;
	case FILM_NEGATIVE:
		fprintf(logfile, "TamanoirImgProc::%s:%d : blur original color image...\n", 
			__func__, __LINE__);
		if(!origBlurredImage) {
			origBlurredImage = cvCreateImage(cvSize(originalImage->width, originalImage->height),
				originalImage->depth, originalImage->nChannels);
			
			int orig_smooth_size = 1 + 2*(int)(4 * m_dpi / 2400);

			// First, blur the color image
			cvSmooth( originalImage, origBlurredImage,
				CV_GAUSSIAN, //int smoothtype=CV_GAUSSIAN,
				orig_smooth_size, orig_smooth_size); //int param1=3, int param2=0 );
					 // FIXME : adapt size to resolution
		}
		m_progress = 35;
		
		
		fprintf(logfile, "TamanoirImgProc::%s:%d : blur grayscaled image (Open)...\n", 
			__func__, __LINE__);
		int open_iter = m_dpi / 2400;
		if(open_iter < 1) open_iter = 1;
		
		cvSmooth(grayImage, medianImage, 
			CV_GAUSSIAN, //int smoothtype=CV_GAUSSIAN,
			m_smooth_size, m_smooth_size );
		/*
		tmOpenImage(
			grayImage,  // => src 
			medianImage, // => dest
			diffImage, // => tmp
			open_iter);
		*/
		fprintf(logfile, "TamanoirImgProc::%s:%d : blur grayscaled image OK\n", 
			__func__, __LINE__);
		break;
	}
		// For debug, save image in temporary directory
		if(g_debug_savetmp) tmSaveImage(TMP_DIRECTORY "medianImage" IMG_EXTENSION, medianImage);
	
	m_progress = 40;
	
	
	
	// ********* DIFFERENCE BETWEEN ORIGINAL AND BLURRED ****************
	fprintf(logfile, "TamanoirImgProc::%s:%d : difference processing image...\n", 
		__func__, __LINE__);
		memset(diffImage->imageData, 0, diffImage->widthStep*diffImage->height);
	
	// Difference histogram
	unsigned long diffHisto[256];
	memset(diffHisto, 0, sizeof(unsigned long)*256);
	
	processDiff(m_FilmType, grayImage, medianImage, diffImage, diffHisto);
	
	// Process difference histogram analysis : 
	unsigned long maxHisto = 0;
	double cdgH = 0;
	unsigned long cdgnb = 0;
	
	int indexMaxHisto = 0;
	
	for(int h=1; h<256; h++) {
		cdgH += h * diffHisto[h];
		cdgnb += diffHisto[h];
		if(diffHisto[h]>maxHisto) {
			indexMaxHisto = h;
			maxHisto = diffHisto[h];
		}
	}
	if(cdgnb>0)
		cdgH /= (double)cdgnb;
	m_threshold = (u8)roundf(cdgH * 2.0);
	
	fprintf(logfile, "TamanoirImgProc::%s:%d : Process histogram : cdgH = %g threshold=%d\n",
			__func__, __LINE__, cdgH, (int)m_threshold);
	
	
	m_progress = 45;
	
	// If image is really noisy, for example with B&W grain (or High sensitivity films), pre-process a 3x3 blur filter
	if( cdgH >= 3.0 ) {
		fprintf(logfile, "TamanoirImgProc::%s:%d : GRAIN ERASER : Process 3x3 blur filter on input image ...\n",
			__func__, __LINE__);
		cvSmooth(grayImage, diffImage, 
			CV_GAUSSIAN, //int smoothtype=CV_GAUSSIAN,
			3, 3 );
		m_progress = 50;

		memcpy(grayImage->imageData, diffImage->imageData, 
			diffImage->widthStep*diffImage->height);

		unsigned long diffHisto2[256];
		memset(diffHisto2, 0, sizeof(unsigned long)*256);
		memset(diffImage->imageData, 0, diffImage->widthStep*diffImage->height);
		
		processDiff(m_FilmType, grayImage, medianImage, diffImage, diffHisto2);
	}
	m_progress = 60;
	
	if(m_threshold < 4)
		m_threshold = 4;
	
	
	unsigned char * diffImageBuffer = (unsigned char *)diffImage->imageData;
	unsigned char * grayImageBuffer = (unsigned char *)grayImage->imageData;
	int width = diffImage->widthStep;
	int height = diffImage->height;
	int pos;
	
	switch(m_FilmType) {
	default: 
		for(pos=0; pos<width*height; pos++) 
		{
			u8 diff = (u8)diffImageBuffer[pos];
		
			if(diff >= m_threshold) 
				diffImageBuffer[pos] = DIFF_THRESHVAL;
		}
		break;
	case FILM_NEGATIVE: 
		for(pos=0; pos<width*height; pos++) 
		{
			u8 diff = (u8)diffImageBuffer[pos];
			
			if( (diff >= m_threshold && grayImageBuffer[pos]>164) ) // Opaque pixels are white
				diffImageBuffer[pos] = DIFF_THRESHVAL;
		}
		break;
	case FILM_POSITIVE: 
		for(pos=0; pos<width*height; pos++) 
		{
			u8 diff = (u8)diffImageBuffer[pos];
		
			if(diff >= m_threshold) 
				diffImageBuffer[pos] = DIFF_THRESHVAL;
			else
				if(diff >= tmmax(3, m_threshold-2))
					diffImageBuffer[pos] = DIFF_CONTOUR;
		}
		break;
	}
		// For debug, save image in temporary directory
		if(g_debug_savetmp) tmSaveImage(TMP_DIRECTORY "diffImage" IMG_EXTENSION, diffImage);
	m_progress = 70;
	
	// Do a close operation on diffImage
	tmCloseImage(diffImage, medianImage, grayImage, 1);
		// For debug, save image in temporary directory
		if(g_debug_savetmp) tmSaveImage(TMP_DIRECTORY "diffImage-Closed" IMG_EXTENSION, medianImage);
	memcpy(diffImage->imageData, medianImage->imageData, medianImage->widthStep * medianImage->height);
	
	m_progress = 80;
	int hThresh = (u8)roundf(cdgH * 3.0);;
	
	
	fprintf(logfile, "TamanoirImgProc::%s:%d : init dust detector...\n", 
		__func__, __LINE__); fflush(stderr);
	
	
	
	
	// Difference 
	fprintf(logfile, "TamanoirImgProc::%s:%d : create grow image...\n", 
		__func__, __LINE__); fflush(stderr);
	if(!growImage)
		growImage = cvCreateImage(cvSize(originalImage->width, originalImage->height),
			IPL_DEPTH_8U, 1);
	memset(growImage->imageData, 0, growImage->widthStep * originalImage->height);
	
	fprintf(logfile, "TamanoirImgProc::%s:%d : create crop images....\n", 
		__func__, __LINE__); fflush(stderr);
	
	// Cropped image
	processingSize = cvSize( tmmin(200, originalImage->width), 
							tmmin(200, originalImage->height));
	
	if(!cropImage) cropImage = cvCreateImage(processingSize,IPL_DEPTH_8U, 1);
	if(!tmpCropImage) tmpCropImage = cvCreateImage(processingSize,IPL_DEPTH_8U, 1);
	if(!cropColorImage) cropColorImage = cvCreateImage(processingSize,IPL_DEPTH_8U, originalImage->nChannels);
	
	if(!sobelImage) sobelImage = cvCreateImage(processingSize,IPL_DEPTH_16S, 1);
	
	
	
	
	
	// Insert difference histogram in cropColorImage
	fprintf(logfile, "TamanoirImgProc::%s:%d :Insert difference histogram in cropColorImage...\n", 
		__func__, __LINE__); fflush(stderr);
	u8 * colorCropImageBuffer = (u8 *)cropColorImage->imageData;
	int pitch = cropColorImage->widthStep;
	int depth = cropColorImage->nChannels;
	memset(colorCropImageBuffer, 0, pitch*processingSize.height);
	m_progress = 90;
	for( int h=0; h<tmmin(256, processingSize.width); h++) {
		if(diffHisto[h]>1) {
			// Use log histogram
			int LogH = (int)(log(diffHisto[h])/log(maxHisto) * processingSize.height);
			// Insert in image
			u8 color = (u8)(h == cdgH ? 127 : (h == hThresh ? 0 : 255)); 
			for(int r=0; r<LogH; r++) 
				colorCropImageBuffer[ (cropColorImage->height-1-r)*pitch + h*depth ] = color;
		}
	}
	
	
	if(!dilateImage) dilateImage = cvCreateImage(processingSize,IPL_DEPTH_8U, 1);
	if(!correctImage) correctImage = cvCreateImage(processingSize,IPL_DEPTH_8U, 1);
	
	if(originalImage->nChannels == 1)
		correctColorImage = correctImage;
	else {
		if(correctColorImage != correctImage && correctColorImage)
			cvReleaseImage(&correctColorImage);
		correctColorImage = cvCreateImage(processingSize,IPL_DEPTH_8U, originalImage->nChannels);
	}

	m_lock = false;

	setResolution(m_dpi);
	
	m_progress = 100;
	
	return 0;
}
/*
 Epson 4990 :
	N&B, 2400dpi, 400ISO: Tile 225,344 : diff_mean=2.24707 variance = 6.97754
	Provia 100, 2400 dpi, Tile 284,186 : diff_mean=1.09766 variance = 2.06445
	
 
*/

int TamanoirImgProc::findUniform(float * p_mean, float  * p_diff_mean, float * p_variance) {
	if(!grayImage) return -1;
	
	int tile_x, tile_y, tile_step = 32;
	u8 * buffer = (u8 *)grayImage->imageData;
	int pitch = grayImage->widthStep;
	int width = grayImage->width;
	int height = grayImage->height;
	
	for(tile_y = height/10; tile_y < height*9/10-tile_step*2; tile_y += tile_step ) {
		
		for(tile_x = width/10; tile_x < width*9/10-tile_step*2; tile_x += tile_step) {
			int x, y;
			int val_topleft = buffer[tile_y * pitch + tile_x];
			long val_mean = 0;
			bool resume = true;
			// Get mean of pixel values
			for(y=tile_y; y<tile_y+tile_step; y++) {
				
				for(x=tile_x; x<tile_x+tile_step; x++) {
					int val = buffer[y * pitch + x];
					if(abs(val - val_topleft) > 16) {
						// Break
						x = width;
						y = height;
						
						resume = false;
					}
					
					val_mean += val;
				}
			}
			
			// Get dispersion
			if(resume) {
				val_mean = (int)roundf((float)val_mean / (float)(tile_step * tile_step));
				if(val_mean > 64) { // Else we may be in noise
					long val_diff = 0;
					long val_diff2 = 0;
					// Get mean of pixel values
					for(y=tile_y; y<tile_y+tile_step; y++) {
						
						for(x=tile_x; x<tile_x+tile_step; x++) {
							int val = buffer[y * pitch + x];
							
							int diff = abs(val - val_mean);
							val_diff += diff;
							val_diff2 += diff * diff;
						}
					}
					
					float diff_mean = (float)val_diff / (float)(tile_step * tile_step);
					float variance = (float)val_diff2 / (float)(tile_step * tile_step);
					// If variance is low, choose this region
					
		//			fprintf(stderr, "Tile %d,%d : variance = %g", tile_x, tile_y, variance);
					if(diff_mean < 4) {
						fprintf(stderr, "Tile %d,%d : diff_mean=%g variance = %g\n", tile_x, tile_y, diff_mean, variance);
						
						if(diff_mean < 3) {
							*p_diff_mean = diff_mean;
							*p_variance = variance;
							return 0;
						}
					}
				}
			}
		}
	}
	
	return -1;
}

int TamanoirImgProc::saveFile(const char * filename) {
	int retry = 0;
	while(retry < 10 && m_lock) {
		sleep(1); retry++;
		fprintf(stderr, "[imgproc]::%s:%d : locked !!\n", __func__, __LINE__);
	}
	if(m_lock)
		return -1;
	
	m_lock = true;
	fprintf(logfile, "TamanoirImgProc::%s:%d : saving '%s'...\n", 
		__func__, __LINE__, filename);

		// Save for debug
		if(g_debug_savetmp) {
			tmSaveImage(TMP_DIRECTORY "growImage_final" IMG_EXTENSION, growImage);
			tmSaveImage(TMP_DIRECTORY "diffImage_final" IMG_EXTENSION, diffImage);
		}

	if(originalImage) {
		tmSaveImage(filename, originalImage);
		m_lock = false;
		return 0;
	}
	m_lock = false;
	return -1;
}



/*
 * Activate/desactivate hot pixels filtering
 */
bool TamanoirImgProc::setHotPixelsFilter(bool on) {
	if(m_hotPixels == on) return on;
	
	fprintf(logfile, "[TamanoirImgProc] %s:%d : %s hot pixels filtering\n", __func__, __LINE__,
		(on ? "ACTIVATE" : "DESACTIVATE" ) );
	m_hotPixels = on ;
	
	
	m_dust_area_min = DUST_MIN_SIZE;
	if(m_hotPixels) {
		m_dust_area_min = HOTPIXEL_MIN_SIZE;
	}
	// Then re-process file
	fprintf(logfile, "TamanoirImgProc::%s:%d : re-preprocessing image...\n", 
		__func__, __LINE__);
	if(originalImage && on)
	{
		fprintf(logfile, "TamanoirImgProc::%s:%d : re-preprocessing image...\n", 
			__func__, __LINE__);
		preProcessImage();
		
		fprintf(logfile, "TamanoirImgProc::%s:%d : re-preprocessing image...\n", 
			__func__, __LINE__);
		firstDust();
	}
	fprintf(logfile, "TamanoirImgProc::%s:%d : re-preprocessing image...\n", 
			__func__, __LINE__);
		
	return m_hotPixels;
}


/*
 * Activate/desactivate trust on good corrections
 */
bool TamanoirImgProc::setTrustCorrection(bool on) {
	fprintf(logfile, "[TamanoirImgProc] %s:%d : %s trust on good correction proposals (last=%d,%d)\n",
		__func__, __LINE__,
		(on ? "ACTIVATE" : "DESACTIVATE" ),
		m_last_correction.crop_x, m_last_correction.crop_y);
	
	if(m_trust != on) { // We changed to trust mode, return to last correction
		if(on) {
			m_seed_x = m_last_correction.crop_x;
			m_seed_y = m_last_correction.crop_y;
		}
	}
	
	m_trust = on;
	
	return m_trust;
}

void TamanoirImgProc::setFilmType(int type) {
	if(m_FilmType == type)
		return;
	
	m_FilmType = type; 
	
	// Then re-process file
	if(originalImage) {
		fprintf(logfile, "TamanoirImgProc::%s:%d : re-preprocessing image...\n", 
			__func__, __LINE__);
		preProcessImage();
		
		fprintf(logfile, "TamanoirImgProc::%s:%d : go to first durst...\n", 
			__func__, __LINE__);
		firstDust();
		
		fprintf(logfile, "TamanoirImgProc::%s:%d : done.\n", 
			__func__, __LINE__);
		
	}
}

int TamanoirImgProc::setResolution(int dpi) {
	
	// Then re-process file
	fprintf(logfile, "TamanoirImgProc::%s:%d : set scan resolution to %d dpi\n", 
		__func__, __LINE__, dpi);
	if(originalImage) {
		// No need to pre-process again the image
		// We just reset the dust seeker
		// Allocate region growing structs
		m_dust_area_min = DUST_MIN_SIZE * dpi / 2400;
		if(m_hotPixels) {
			m_dust_area_min = HOTPIXEL_MIN_SIZE;
		}
		
		fprintf(logfile, "TamanoirImgProc::%s:%d : dust min area %d pixels^2\n", 
			__func__, __LINE__, m_dust_area_min);
		
		m_dust_area_max = 800 * dpi / 2400;
		if(dpi != m_dpi) {
			preProcessImage();
			firstDust();
		}
		
	}
	m_dpi = dpi;
	fprintf(logfile, "TamanoirImgProc::%s:%d : scan resolution = %d\n", 
		__func__, __LINE__, m_dpi);
	
	return m_dpi;
}



/* @brief Get progress in %age */
int TamanoirImgProc::getProgress() {
	if(!grayImage) return 0;
	if(!grayImage->height) return 0;

	return m_progress;
}

int TamanoirImgProc::firstDust() 
{
	// Reset last dust position
	m_seed_x = m_seed_y = 0;
	m_progress = 0;
	
	// Clear buffer of known dusts
	if(growImage)
		memset(growImage->imageData, 0, growImage->widthStep * growImage->height);
	
	// Disable trust for first image
	bool old_trust = m_trust;
	int ret = nextDust();
	m_trust = old_trust;
	
	return ret;
}

int TamanoirImgProc::nextDust() {
	int x, y;
	int pos;
	if(!diffImage) return -1;
	
	int retry = 0;
	while(retry < 10 && m_lock) {
		sleep(1); retry++;
		fprintf(stderr, "[imgproc]::%s:%d : locked !!\n", __func__, __LINE__);
	}
	if(m_lock)
		return -1;
	
	m_lock = true;
	
	
	int width = diffImage->width;
	int height = diffImage->height;
	
	u8 * diffImageBuffer = (u8 *)diffImage->imageData;
	u8 * growImageBuffer = (u8 *)growImage->imageData;
	
	if(g_debug_imgverbose)
		fprintf(logfile, "TamanoirImgProc::%s:%d : searching seed from %d,%d ...\n", 
						__func__, __LINE__, m_seed_y, m_seed_x);
	
	memset(&m_correct, 0, sizeof(t_correction));
	
	memset(&m_lastDustComp, 0, sizeof(CvConnectedComp));
	
	m_correct.dest_x = -1; // Clear stored correction
	
	
	for(y = m_seed_y; y<height; y++) {
		pos = y * diffImage->widthStep + m_seed_x;
		for(x = m_seed_x; x<width-1; x++, pos++) {
			
			if(diffImageBuffer[pos] == DIFF_THRESHVAL && !growImageBuffer[pos])
			// Grow region here if the region is big enough
			if(diffImageBuffer[pos+1]==DIFF_THRESHVAL || m_hotPixels) 
//			|| diffImageBuffer[pos+diffImage->widthStep]==DIFF_THRESHVAL) 
			{
				int return_now = findDust(x,y);
			
				if(return_now > 0) {
					m_lock = false;
					return return_now;
				}
			}
			
		}
		
		
		m_seed_x = 0;
	}
	
	m_lock = false;
	return 0;
}


	
int TamanoirImgProc::findDust(int x, int y) {
	CvConnectedComp connect;
	memset(&connect, 0, sizeof(CvConnectedComp));
	
	u8 * diffImageBuffer = (u8 *)diffImage->imageData;
	u8 * growImageBuffer = (u8 *)growImage->imageData;
	
	
	// Process a region growing 
	tmGrowRegion( diffImageBuffer, growImageBuffer, 
		diffImage->widthStep, diffImage->height, 
		x, y, 
		DIFF_THRESHVAL,
		255,
		&connect );
	
	if(g_debug_imgverbose > 1) {
		fprintf(logfile, "TamanoirImgProc::%s:%d : found seed at %d,%d surf=%d  %d >? %d <? %d...\n", 
				__func__, __LINE__, x, y, (int)connect.area,
				m_dust_area_min, (int)connect.area, m_dust_area_max);
		fprintf(logfile, "TamanoirImgProc::%s:%d : => %d,%d+%dx%d...\n", 
				__func__, __LINE__,
				connect.rect.x,connect.rect.y,
				connect.rect.width,
					connect.rect.height);
	}
	
	
	
	
	if(connect.area >= m_dust_area_min &&
	   connect.area < m_dust_area_max)
	{
		// There a seed here !!!
		m_seed_x = x+1;
		m_seed_y = y;
		
		int connect_area = (int)connect.area;
		
		/** Update stats */
		m_dust_stats.nb_grown++;
		if(connect_area<STATS_MAX_SURF) {
			m_dust_stats.grown_size[connect_area]++;
		}

		
		int connect_width = connect.rect.width;
		int connect_height = connect.rect.height;
		
		// Crop area geometry
		int crop_width = cropImage->width;
		int crop_height = cropImage->height;
		
		int crop_x = connect.rect.x+connect.rect.width/2 - crop_width/2;
		if(crop_x < 0) 		crop_x = 0;
		if(crop_x + crop_width >= originalImage->width)
			crop_x = originalImage->width - crop_width-1;
		
		int crop_y = connect.rect.y+connect.rect.height/2 - crop_height/2;
		if(crop_y < 0) 		crop_y = 0;
		if(crop_y + crop_height >= originalImage->height)
			crop_y = originalImage->height - crop_height-1;
		
		// Real position of dust in cropped image
		int crop_center_x = x - crop_x;
		int crop_center_y = y - crop_y;
		
		if(g_debug_imgverbose > 1)
			fprintf(logfile, "TamanoirImgProc::%s:%d : grown dust at %d,%d+%dx%d => %d,%d in cropped image...\n", 
				__func__, __LINE__,
				connect.rect.x,connect.rect.y,
				connect.rect.width, connect.rect.height,
				crop_center_x, crop_center_y
				);


		// ------- Cropped images are used to lower the memory needs / and for GUI display of correction proposals
		// Re-greow region in cropped image
		tmCropImage(diffImage, cropImage, 
					crop_x, crop_y);
		if(g_debug_savetmp) {
			tmSaveImage(TMP_DIRECTORY "a-diffImage" IMG_EXTENSION, 
						cropImage);
		}
		
		// clear grow buffer
		memset(correctImage->imageData, 0, 
			correctImage->widthStep * correctImage->height);
		
		// Grow again region in cropped image
		CvConnectedComp crop_connect;
		tmGrowRegion(
			(u8 *)cropImage->imageData, 
			(u8 *)correctImage->imageData, 
			cropImage->widthStep, cropImage->height, 
			crop_center_x, crop_center_y, 
			DIFF_THRESHVAL,
			255,
			&crop_connect);
		
		
		if(crop_connect.area >= m_dust_area_min &&
		   crop_connect.area < m_dust_area_max)
		{
			/* LOW LEVEL DEBUG ONLY : WHEN NOT COMENTED, IT DESTROY THE INPUT IMAGE !! 
			if(g_debug_savetmp)
			{
				cvRectangle(grayImage, 
						cvPoint(connect.rect.x-connect.rect.width,
							connect.rect.y-connect.rect.height), 
						cvPoint(connect.rect.x+connect.rect.width,
							connect.rect.y+connect.rect.height),
						cvScalarAll(0), 1);
				tmCropImage(grayImage, cropImage, 
						connect.rect.x+connect.rect.width/2,
						connect.rect.y+connect.rect.height/2);
			
				tmSaveImage(TMP_DIRECTORY "cropImage" IMG_EXTENSION, cropImage);
			}*/
			
			
			
			
			// Check if copy is not along a contour
			//int dx = abs(copy_src_x - copy_dest_x);
			//int dy = abs(copy_src_y - copy_dest_y);
			//if(dx < dy) // Check if we are not copying while following a border
			//	cvSobel(correctImage, sobelImage, 1, 0, 5);
			//else
			//	cvSobel(correctImage, sobelImage, 0, 1, 5);
			
			tmCropImage(grayImage, 
				cropImage, 
				crop_x, crop_y);
			
			
			/* TRY TO DETECT COPY FROM PATTERNS WITH CONTOUR DETECTION
			// Use absolute value of Sobel
			cvSobel(cropImage, sobelImage, 1, 0, 5);
			cvConvertScaleAbs(sobelImage, tmpCropImage, 0.25, 0);
			cvSobel(cropImage, sobelImage, 0, 1, 5);
			cvConvertScaleAbs(sobelImage, dilateImage, 0.25, 0);
			
			// Compare direction of contour at detected dust center
			u8 val_horiz = *(u8 *)(tmpCropImage->imageData +
					(int)(crop_connect.rect.y+crop_connect.rect.height/2) * tmpCropImage->widthStep
					+ (int)(crop_connect.rect.x+crop_connect.rect.width/2));
			u8 val_vert = *(u8 *)(dilateImage->imageData +
					(int)(crop_connect.rect.y+crop_connect.rect.height/2) * dilateImage->widthStep
					+ (int)(crop_connect.rect.x+crop_connect.rect.width/2));
			
			fprintf(stderr, "TmImgProc::%s:%d : %d,%d %dx%d => horiz=%d vert=%d\n",
					__func__, __LINE__,
					x,y,
					(int)crop_connect.rect.width, (int)crop_connect.rect.height,
					val_horiz, val_vert);
			u8 * sobelImageBuffer = NULL;
			if(val_vert > val_horiz)
				sobelImageBuffer = (u8 *)dilateImage->imageData;
			else
				sobelImageBuffer = (u8 *)tmpCropImage->imageData;
			*/
			
			
			
			// => this dilated image will be used as mask for correlation search
			// but we have to fill the center of the dust 
			//	A full circle will only give us a empty circle : O
			unsigned char * dilateImageBuffer = (unsigned char *)dilateImage->imageData;
			unsigned char * correctImageBuffer = (unsigned char *)correctImage->imageData;
			
			// Crop median image
			tmCropImage(medianImage, 
				tmpCropImage, 
				crop_x, crop_y);
			// Do a dilatation around the grown 
			tmDilateImage(correctImage, dilateImage);
			
			
			int r,c, histoDiff[512];
			memset(histoDiff, 0, 512*sizeof(int));
			for(r=crop_connect.rect.y;r<crop_connect.rect.y+crop_connect.rect.height; r++) {
				int crop_pos = r*dilateImage->widthStep + crop_connect.rect.x;
				for(c=crop_connect.rect.x; c<crop_connect.rect.x+crop_connect.rect.width;
						c++,crop_pos++) {
					if(dilateImageBuffer[crop_pos]) {
						// Difference between image and median
						//int dif = (int)correctImageBuffer[crop_pos]-(int)tmpCropImageBuffer[crop_pos];
						histoDiff[ (int)correctImageBuffer[crop_pos] ]++;
					}
				}
			}
			
			int min_dif = 255;
			int max_dif = -255;
			for(int h=0; h<256; h++) 
				if(histoDiff[h])
				{
					if(h<min_dif) min_dif=h;
					if(h>max_dif) max_dif=h;
				}
			
			//fprintf(stderr, "::%s:%d : min/max = %d / %d\n", __func__, __LINE__, min_dif, max_dif);
			for(r=crop_connect.rect.y;r<crop_connect.rect.y+crop_connect.rect.height; r++) {
				int crop_pos = r*dilateImage->widthStep + crop_connect.rect.x;
				for(c=crop_connect.rect.x; c<crop_connect.rect.x+crop_connect.rect.width; c++,crop_pos++) {
					if(correctImageBuffer[crop_pos]>=min_dif
						&& correctImageBuffer[crop_pos]<=max_dif) {
						dilateImageBuffer[crop_pos] = 255;
					}
				}
			}
			
			
			
			// Process search around the dust in original image
			// Use center x,y - width,height
			connect_width = crop_connect.rect.width;
			connect_height = crop_connect.rect.height;
			int connect_center_x = crop_connect.rect.x + connect_width/2;
			int connect_center_y = crop_connect.rect.y + connect_height/2;
			
			while(connect_width < 5 
				&& (connect_center_x+connect_width)<crop_width-3) {
				connect_width += 2;
			}
			while(connect_height < 5 
				&& (connect_center_y+connect_height)<crop_height-3) {
				connect_height += 2;
			}
			
			int copy_dest_x, copy_dest_y,
				copy_src_x, copy_src_y,
				copy_width, copy_height;
			int best_correl = 10000;
			
			
			// Crop original image
			switch(m_FilmType) {
			default:
			case FILM_POSITIVE:
				tmCropImage(originalImage, 
						correctColorImage, 
						crop_x, crop_y);
				break;
			case FILM_UNDEFINED:
			case FILM_NEGATIVE:
				if(origBlurredImage) 
					tmCropImage(origBlurredImage, 
						correctColorImage, 
						crop_x, crop_y);
				else
					tmCropImage(originalImage, 
						correctColorImage, 
						crop_x, crop_y);
				break;
			}
			
			// Find best proposal in originalImage
			int ret = tmSearchBestCorrelation(
				correctColorImage, dilateImage, 
				connect_center_x, connect_center_y,
				connect_width, connect_height, 
				&copy_dest_x, &copy_dest_y,
				&copy_src_x, &copy_src_y,
				&copy_width, &copy_height,
				&best_correl);
			
			if(ret>0) {
				m_lastDustComp = connect; 
				u8 return_now = 1;
				
				/** Update stats */
				m_dust_stats.nb_grown_replaced++;
				if(connect_area<STATS_MAX_SURF) {
					m_dust_stats.grown_size_replaced[connect_area]++;
				}
				
				// If the source and the destination are in the same extended grown region,
				
				// Re-greow region in cropped image
				tmCropImage(diffImage, cropImage, 
							crop_x, crop_y);
				// clear grow buffer
				memset(correctImage->imageData, 0, 
					correctImage->widthStep * correctImage->height);
				
				// Grow again region in cropped image
				CvConnectedComp ext_connect;
				tmGrowRegion(
					(u8 *)cropImage->imageData, 
					(u8 *)correctImage->imageData, 
					cropImage->widthStep, cropImage->height, 
					crop_center_x, crop_center_y, 
					DIFF_CONTOUR,
					42, // The answer of Life, God, and 
					&ext_connect);
				
				float src_failure = tmNonZeroRatio(correctImage,
						copy_src_x, copy_src_y,
						copy_width, copy_height,
						copy_width,copy_height,0,0,// Exclusion ROI
						42);
				if(src_failure) {
					
					//fprintf(stderr, "")
					return_now = 0;
				}
				
				// Store correction in full image buffer
				m_correct.dest_x = crop_x + copy_dest_x;
				m_correct.dest_y = crop_y + copy_dest_y;
				m_correct.copy_width = copy_width;
				m_correct.copy_height = copy_height;
				
				// Relative storage
				m_correct.crop_x = crop_x;
				m_correct.crop_y = crop_y;
				m_correct.rel_src_x = copy_src_x;
				m_correct.rel_src_y = copy_src_y;
				m_correct.rel_dest_x = copy_dest_x;
				m_correct.rel_dest_y = copy_dest_y;
				
				// Update dest
				m_correct.src_x = m_correct.crop_x + m_correct.rel_src_x;
				m_correct.src_y = m_correct.crop_y + m_correct.rel_src_y;
				m_correct.area = connect_area;
				
				// Fill size statistics
				m_correct.area = (int)connect.area;
				m_correct.width_mm = 25.4f * connect.rect.width / m_dpi;
				m_correct.height_mm = 25.4f * connect.rect.height / m_dpi;
				
				
				if(m_trust && return_now) {
					// Check if correction area is in diff image
					int left = tmmin(copy_src_x, copy_dest_x);
					int right = tmmax(copy_src_x + copy_width, copy_dest_x + copy_width);
					int top = tmmin(copy_src_y, copy_dest_y);
					int bottom = tmmax(copy_src_y + copy_height, copy_dest_y + copy_height);
					
					float fill_failure = tmNonZeroRatio(diffImage,
						crop_x + left, crop_y + top,
						right - left, bottom - top,
						crop_x + copy_dest_x, crop_y + copy_dest_y,
						copy_width, copy_height
						);
					//	crop_x + left, crop_y + top,
					//	right - left, bottom - top);
					
					//fprintf(stderr, "[imgproc] %s:%d : fill_failure=%g\n", 
					//	__func__, __LINE__, fill_failure);
					
					// If we can trust the correction proposal, let's correct know !
					if( best_correl < TRUST_CORREL_MAX
						&& m_correct.area < TRUST_AREA_MAX
						&& fill_failure == 0) {
						
						forceCorrection(m_correct, true);
						return_now = 0;
					} 
					
				}
				
					
				if(return_now) {
					
					//fprintf(stderr, "[imgproc] %s:%d : return 1\n", __func__, __LINE__);
					return 1;
				}
			} else {
				
				// DEBUG FUNCTIONS
				if(g_debug_imgverbose > 1) {
					fprintf(logfile, "TamanoirImgProc::%s:%d : dust at %d,%d+%dx%d "
						"=> %d,%d in cropped image => no replace candidate (best=%d)...\n", 
						__func__, __LINE__,
						connect.rect.x,connect.rect.y,
						connect.rect.width, connect.rect.height,
						crop_center_x, crop_center_y, best_correl
						);
					if(g_debug_savetmp)
					{
						tmSaveImage(TMP_DIRECTORY "failed-dilateImage" IMG_EXTENSION,  
								dilateImage);
					}
				}
				
				// Mark failure on displayImage
				if(g_debug_imgoutput && displayImage) {
					int disp_x = (crop_x + crop_connect.rect.x) * displayImage->width / grayImage->width;
					int disp_y = (crop_y + crop_connect.rect.y) * displayImage->height / grayImage->height;
					int disp_w = crop_connect.rect.width * displayImage->width / grayImage->width;
					int disp_h = crop_connect.rect.height * displayImage->height / grayImage->height;
					tmMarkFailureRegion(displayImage, 
							disp_x, disp_y, disp_w, disp_h, COLORMARK_FAILED);
				}
				
				if(g_debug_imgoutput) {

					tmMarkFailureRegion(originalImage, 
						crop_x + crop_connect.rect.x, crop_y + crop_connect.rect.y,
						crop_connect.rect.width, crop_connect.rect.height, 120);
					tmMarkFailureRegion(diffImage, 
						crop_x + crop_connect.rect.x, crop_y + crop_connect.rect.y,
						crop_connect.rect.width, crop_connect.rect.height, 120);
				}
				// END OF DEBUG FUNCTIONS
				
			}
			
			// DEBUG FUNCTIONS
			if(g_option_stopoguess || g_debug_correlation) {
				// Diff image for display in GUI
				tmCropImage(diffImage, 
						cropImage, 
						crop_x, crop_y);
				
				tmCropImage(originalImage, cropColorImage, 
						crop_x, crop_y);
				return 1;
			}
			// END OF DEBUG FUNCTIONS
			
			// For debug, save image in temporary directory
			//tmSaveImage(TMP_DIRECTORY "diffImageGrown.pgm", growImage);
		} else {
			if(g_debug_imgverbose > 1) {
				fprintf(logfile, "TamanoirImgProc::%s:%d : dust regrow=> %d,%d+%dx%d "
					"=> %d,%d in cropped image => PROBLEM : NOT FOUND WITH SAME THRESHOLD...\n", 
					__func__, __LINE__,
					crop_connect.rect.x, crop_connect.rect.y,
					crop_connect.rect.width, crop_connect.rect.height,
					crop_center_x, crop_center_y
					);
			}
			
			if(g_debug_imgoutput) {
				tmMarkFailureRegion(originalImage, 
					crop_x + crop_connect.rect.x, crop_y + crop_connect.rect.y,
					crop_connect.rect.width, crop_connect.rect.height, 32);
			}
		}
	}
	
	
	return 0;
}


void TamanoirImgProc::cropViewImages() {
	cropCorrectionImages(m_correct);
}


void TamanoirImgProc::cropCorrectionImages(t_correction correction) {
	if(correction.copy_width <= 0) return;
	
	if(!originalImage) {
		fprintf(logfile, "TamanoirImgProc::%s:%d : no originalImage processingSize %dx%d\n",
				__func__, __LINE__, processingSize.width, processingSize.height);
		return;
	}
	if(!diffImage) {
		fprintf(logfile, "TamanoirImgProc::%s:%d : no diffImage processingSize %dx%d\n",
				__func__, __LINE__, processingSize.width, processingSize.height);
		return;
	}
	if(processingSize.width * processingSize.height <=0) {
		fprintf(logfile, "TamanoirImgProc::%s:%d : error in processingSize %dx%d\n",
				__func__, __LINE__, processingSize.width, processingSize.height);
		return;
	}
	// Allocate images
	if(!disp_cropColorImage) 
		disp_cropColorImage = cvCreateImage(processingSize,IPL_DEPTH_8U, originalImage->nChannels);
	if(!disp_correctColorImage) 
		disp_correctColorImage = cvCreateImage(processingSize,IPL_DEPTH_8U, originalImage->nChannels);
	
	if(!disp_cropImage) 
		disp_cropImage = cvCreateImage(processingSize,IPL_DEPTH_8U, 1);
		
	if(0) { // Sobel
		if(!disp_dilateImage) 
		{
			disp_dilateImage = cvCreateImage(processingSize, IPL_DEPTH_16S, 1);
			memset( disp_dilateImage->imageData, 0, 
					disp_dilateImage->widthStep * disp_dilateImage->height * sizeof(short));
		}
		
		// Get Sobel
		tmCropImage(grayImage, disp_cropImage, 
					correction.crop_x, correction.crop_y);
		int dx = abs(correction.rel_src_x - correction.rel_dest_x);
		int dy = abs(correction.rel_src_y - correction.rel_dest_y);
		if(dx < dy) // Check if we are not copying while following a border
			cvSobel(disp_cropImage, disp_dilateImage, 1, 0, 5);
		else
			cvSobel(disp_cropImage, disp_dilateImage, 0, 1, 5);
	} else {
		
		if(!disp_dilateImage) 
		{
			disp_dilateImage = cvCreateImage(processingSize, IPL_DEPTH_8U, 1);
			memset( disp_dilateImage->imageData, 0, 
					disp_dilateImage->widthStep * disp_dilateImage->height );
		}
		
		// Get Sobel
		unsigned long diffH[256];
		tmCropImage(diffImage, disp_dilateImage, 
					correction.crop_x, correction.crop_y);
		
	}
	
	
// Top-left on GUI : Original image for display in GUI
	tmCropImage(originalImage, disp_cropColorImage, 
				correction.crop_x, correction.crop_y);
	
	if(g_debug_savetmp)
	{
		tmSaveImage(TMP_DIRECTORY "a-cropImage" IMG_EXTENSION, 
			disp_cropColorImage);
		
		
		tmSaveImage(TMP_DIRECTORY "z-cropImageGray" IMG_EXTENSION, 
			disp_cropImage);
	}
	
	
	
// Bottom-Left
	// If we use a blurred version for searching,
	//	update cropped color with original this time
	tmCropImage(originalImage, disp_correctColorImage, 
			correction.crop_x, correction.crop_y);
	
	// Clone image region 
	tmCloneRegion(disp_cropColorImage, 
		correction.rel_dest_x, correction.rel_dest_y, // dest
		correction.rel_src_x, correction.rel_src_y, // src
		correction.copy_width, correction.copy_height,
		disp_correctColorImage
		);
	
	tmMarkCloneRegion(disp_cropColorImage, 
		correction.rel_dest_x, correction.rel_dest_y, // dest
		correction.rel_src_x, correction.rel_src_y, // src
		correction.copy_width, correction.copy_height,
		false // mark move dest
		);
	
	if(g_debug_savetmp)
	{
		tmSaveImage(TMP_DIRECTORY "c-correctImage" IMG_EXTENSION, 
			disp_correctColorImage);
	}
	
// Top-right = dilatation image of dust
	if(g_debug_savetmp)
	{
		tmSaveImage(TMP_DIRECTORY "b-dilateImage" IMG_EXTENSION,  
			disp_dilateImage);
	}
	
// Bottom-right: Diff image for display in GUI
	tmCropImage(diffImage, 
			disp_cropImage, 
			correction.crop_x, correction.crop_y);
	if(g_debug_savetmp)
	{
		tmSaveImage(TMP_DIRECTORY "z-diffImageGray" IMG_EXTENSION, 
			disp_cropImage);
		
	}

	
// Main windows	
	if(displayImage) {
		
		// Return when we can propose something for correction
		// Mark current on displayImage
		int disp_x = (correction.crop_x + correction.rel_dest_x) * displayImage->width / grayImage->width;
		int disp_y = (correction.crop_y + correction.rel_dest_y) * displayImage->height / grayImage->height;
		int disp_w = correction.copy_width * displayImage->width / grayImage->width;
		int disp_h = correction.copy_height * displayImage->height / grayImage->height;
		
		tmMarkFailureRegion(displayImage, 
					disp_x, disp_y, disp_w, disp_h, COLORMARK_CURRENT);
	}
}

void TamanoirImgProc::setCopySrc(int rel_x, int rel_y) {
	setCopySrc(&m_correct, rel_x, rel_y);
}

void TamanoirImgProc::setCopySrc(t_correction * pcorrection, int rel_x, int rel_y) {
	if(pcorrection->crop_x <= 0) return;
	if(!diffImage) return;
	
	if(g_debug_imgverbose)
		fprintf(stderr, "[imgproc] %s:%d : rel_x=%d rel_y=%d  %dx%d\n", __func__, __LINE__,
			rel_x, rel_y, cropImage->width, cropImage->height);
	
	
	// update center of source 
	int x = rel_x - pcorrection->copy_width / 2;
	int y = rel_y - pcorrection->copy_height / 2;
	
	
	// Store correction in full image buffer
	pcorrection->rel_src_x = x;
	pcorrection->rel_src_y = y;
	pcorrection->src_x = pcorrection->crop_x + pcorrection->rel_src_x;
	pcorrection->src_y = pcorrection->crop_y + pcorrection->rel_src_y;


	// Update absolute dest if changed
	pcorrection->dest_x = pcorrection->crop_x + pcorrection->rel_dest_x;
	pcorrection->dest_y = pcorrection->crop_y + pcorrection->rel_dest_y;


	// Update display must be requested by the GUI !
}


/* Apply a former correction */
int TamanoirImgProc::skipCorrection(t_correction correction) {
	if(correction.copy_width <= 0) return 0;
	
	// Update progress
	int y = correction.crop_y + correction.rel_dest_y;
	m_progress = (int)(100 * y / grayImage->height);
	
	memcpy(&m_last_correction, &correction, sizeof(t_correction));
	
	// Mark skip action on displayImage
	if(displayImage) {
		int disp_x = (correction.dest_x ) * displayImage->width / grayImage->width;
		int disp_y = (correction.dest_y ) * displayImage->height / grayImage->height;
		int disp_w = correction.copy_width * displayImage->width / grayImage->width;
		int disp_h = correction.copy_height * displayImage->height / grayImage->height;
	
		tmMarkFailureRegion(displayImage, 
			disp_x, disp_y, disp_w, disp_h, COLORMARK_REFUSED);
	}	
	return 0;
}

/* Apply proposed correction */
int TamanoirImgProc::applyCorrection()
{
	return applyCorrection(m_correct);
}

int TamanoirImgProc::forceCorrection(t_correction correction, bool force)
{
	if(!force) {
		// Update progress
		int y = correction.crop_y + correction.rel_dest_y;
		m_progress = (int)(100 * y / grayImage->height);
		
		memcpy(&m_last_correction, &correction, sizeof(t_correction));
	}
	return applyCorrection(correction);
}	

/* Apply a former correction */
int TamanoirImgProc::applyCorrection(t_correction correction)
{
	if(correction.dest_x < 0)
		return -1; // no available correction
	if(correction.copy_width <= 0)
		return -1; // no available correction
	
	
	
	
	if(g_debug_imgverbose)
		fprintf(logfile, "TamanoirImgProc::%s:%d : Apply clone on original image.\n", 
			__func__, __LINE__);
	
	/** Update stats */
	m_dust_stats.nb_grown_validated++;
	if(correction.area<STATS_MAX_SURF) {
		m_dust_stats.grown_size_validated[correction.area]++;
	}
	
	// Apply clone on original image
	tmCloneRegion(  originalImage, 
			correction.dest_x, correction.dest_y,
			correction.src_x, correction.src_y,
			correction.copy_width, correction.copy_height);
	
	// Delete same region in diff image to never find it again, even if we
	// use the rewind function
	tmFillRegion(  diffImage, 
			correction.dest_x, correction.dest_y,
			correction.copy_width, correction.copy_height,
			DIFF_NEUTRALIZE);
	
	
	if(displayImage) {
		// Mark failure on displayImage
		int disp_x = (correction.dest_x ) * displayImage->width / grayImage->width;
		int disp_y = (correction.dest_y ) * displayImage->height / grayImage->height;
		int disp_w = correction.copy_width * displayImage->width / grayImage->width;
		int disp_h = correction.copy_height * displayImage->height / grayImage->height;
		tmMarkFailureRegion(displayImage, 
				disp_x, disp_y, disp_w, disp_h, COLORMARK_CORRECTED);
	}
	
	// And clear dust at destination position
	// to make this area usable for future copy source
	if(g_debug_imgoutput) {

		tmMarkCloneRegion(originalImage, 
					correction.dest_x, correction.dest_y,
					correction.src_x, correction.src_y,
					correction.copy_width, correction.copy_height);
	}
	
	
	memset(&correction, 0, sizeof(t_correction));
	correction.dest_x = -1;
	return 0;
}



/** Process then print statistics */
void processAndPrintStats(dust_stats_t * dust_stats) {

	if(dust_stats->nb_grown>0) {
		dust_stats->ratio_replaced_grown = (float)dust_stats->nb_grown_replaced/dust_stats->nb_grown;
		dust_stats->ratio_validated_grown = (float)dust_stats->nb_grown_validated/dust_stats->nb_grown;
	}
	
	if(dust_stats->nb_grown_replaced>0) {
		dust_stats->ratio_validated_replaced = (float)dust_stats->nb_grown_validated/dust_stats->nb_grown_replaced;
	}
/*	unsigned long grown_size[STATS_MAX_SURF];				*! grown size histogram */
/*	unsigned long grown_size_replaced[STATS_MAX_SURF];		*! grown size histogram */
/*	unsigned long grown_size_validated[STATS_MAX_SURF];		*! grown size histogram */
	
	if(g_debug_imgverbose) {
		int max_size = 0;
		int max_size_replaced = 0;
		int max_size_validated = 0;
		for(int size=0; size<STATS_MAX_SURF; size++) {
			if(dust_stats->grown_size[size]>1)
				max_size = size;
			if(dust_stats->grown_size_replaced[size]>1)
				max_size_replaced = size;
			if(dust_stats->grown_size_validated[size]>1)
				max_size_validated = size;
			if(dust_stats->grown_size[size]>0)
				fprintf(logfile, "[%d] : %lu / %lu / %lu\n", size, 
					dust_stats->grown_size[size], 
					dust_stats->grown_size_replaced[size], 
					dust_stats->grown_size_validated[size]);
		}
		
		/* Statistics variables */
		fprintf(logfile, "Statistics : \n");
		fprintf(logfile, "\t nb_grown = %lu\n", dust_stats->nb_grown);
		fprintf(logfile, "\t nb_grown_replaced = %lu\n", dust_stats->nb_grown_replaced);
		fprintf(logfile, "\t nb_grown_validated = %lu\n", dust_stats->nb_grown_validated);
		fprintf(logfile, "\n");
	
		fprintf(logfile, "\t ratio_replaced/grown = %g %%\n", 100.f*dust_stats->ratio_replaced_grown);
		fprintf(logfile, "\t ratio_validated/grown = %g %%\n", 100.f*dust_stats->ratio_validated_grown);
		fprintf(logfile, "\t ratio_validated/replaced = %g %%\n", 100.f*dust_stats->ratio_validated_replaced);
		fprintf(logfile, "\n");
		
		fprintf(logfile, "Max sizes :\n");
		fprintf(logfile, "\tMax size: %d\n", max_size);
		fprintf(logfile, "\tMax size replaced: %d\n", max_size_replaced);
		fprintf(logfile, "\tMax size validated: %d\n", max_size_validated);
	}
}









#ifdef PIAF_PLUGIN
void median_proc(unsigned char * imageIn, int buffer_size) 
{
	if((!median_buf || oldmedian != median) && median > 0) {
		if(median_buf) {
			for(int i=0; i<oldmedian; i++)
				delete [] median_buf[i];
			delete []  median_buf;
		}
		median_buf = new unsigned char * [median];
		for (int b=0; b<median; b++) {
			median_buf[b] = new unsigned char [buffer_size];
			
			memcpy(median_buf[b], imageIn, buffer_size);
		}
		pos = 0;
		oldmedian = median;
	}
	else {
		memcpy(median_buf[pos], imageIn, buffer_size);
		pos++;
		if(pos == (int)median) pos = 0;
	}
}

void median_proc_mask(unsigned char * imageIn, unsigned char * imageMask, int buffer_size) 
{
	if((!median_buf || oldmedian != median) && median > 0) {
		if(median_buf) {
			for(int i=0; i<oldmedian; i++)
				delete [] median_buf[i];
			delete []  median_buf;
		}
		median_buf = new unsigned char * [median];
		for (int b=0; b<median; b++) {
			median_buf[b] = new unsigned char [buffer_size];
			
			memcpy(median_buf[b], imageIn, buffer_size);
		}
		pos = 0;
		oldmedian = median;
	}
	else {
		for(long pix=0; pix<buffer_size; pix++) {
			if(imageMask[pix])
				median_buf[pos][pix] = imageIn[pix];
			else {
				unsigned short total = 0;
				for(int i=0; i<(int)median; i++)
					total += (unsigned short)median_buf[i][pix];
				
				unsigned char val = (unsigned char)(total / median);
				
				median_buf[pos][pix] = (unsigned char)(((unsigned short)total + 
					(unsigned short)imageIn[pix])/ (median+1));
			}
		}
		//memcpy(median_buf[pos], imageIn, buffer_size);
		pos++;
		if(pos == (int)median) pos = 0;
	}
}


void median_func()
{
	unsigned char * imageIn  = (unsigned char *)imIn->buffer;
	unsigned char * imageOut = (unsigned char *)imOut->buffer;

	// Append in median loop
	median_proc(imageIn, imIn->buffer_size);
	
	// Generate output
	for(unsigned long r = 0; r < imIn->buffer_size; r++)
	{
		unsigned short total = 0;
		for(int i=0; i<(int)median; i++)
			total += (unsigned short)median_buf[i][r];
		
		unsigned char pix = (unsigned char)(total / median);
		imageOut[r] = pix;
		
	}
}

unsigned char * imageInCopy  = NULL;
unsigned char * imageInMask = NULL;

void median_enhance() 
{
	swImageStruct * imIn = ((swImageStruct *)plugin.data_in);
	swImageStruct * imOut = ((swImageStruct *)plugin.data_out);
	unsigned char * imageIn  = (unsigned char *)imIn->buffer;
	unsigned char * imageOut = (unsigned char *)imOut->buffer;

	if(!imageInCopy)
		imageInCopy = new unsigned char [imIn->buffer_size];
	if(!imageInMask)
		imageInMask = new unsigned char [imIn->buffer_size];
	
	memcpy(imageInCopy, imageIn, imIn->buffer_size);
	memset(imageInMask, 255, imIn->buffer_size);
	
	// Generate output
	if(median_buf)
	for(unsigned long r = 0; r < imIn->buffer_size; r++)
	{
		unsigned short total = 0;
		for(int i=0; i<(int)median; i++)
			total += (unsigned short)median_buf[i][r];
		
		unsigned char pix = (unsigned char)(total / median);
		if(abs((long)imageIn[r] - (long)pix)<15) {
			imageOut[r] = pix;
		}
		else {
			imageOut[r] = pix; //imageIn[r];
			imageInMask[r] = 0;
		}
	}
	
	// Append in median loop
	median_proc_mask(imageInCopy, imageInMask, imIn->buffer_size);
}



float * low_update_buf = NULL;
void low_update() {
	swImageStruct * imIn = ((swImageStruct *)plugin.data_in);
	swImageStruct * imOut = ((swImageStruct *)plugin.data_out);
	unsigned char * imageIn  = (unsigned char *)imIn->buffer;
	unsigned char * imageOut = (unsigned char *)imOut->buffer;
	unsigned long r;
	
	if(!low_update_buf) {
		low_update_buf = new float [ imIn->buffer_size ];
		for(r = 0; r < imIn->buffer_size; r++)
			low_update_buf[r] = (float)imageIn[r];
	}
	
	float rest = 1.f - low_update_rate;
	for(r = 0; r < imIn->buffer_size; r++)
	{
		low_update_buf[r] = low_update_rate * (float)imageIn[r] + rest * low_update_buf[r];
		imageOut[r] = (unsigned char)low_update_buf[r];
	}
}


IplImage * cvImIn = NULL;
IplImage * cvGray = NULL;
IplImage * cvBlurred = NULL;

IplImage * cvDiff = NULL;

void allocateImage(IplImage ** img, int w, int h) {
	// Only in grayscale
	*img = cvCreateImage(cvSize(w,h), IPL_DEPTH_8U, 1);
	memset((*img)->imageData, 0, w*h);
}




void dust_remove() {
	swImageStruct * imIn = ((swImageStruct *)plugin.data_in);
	swImageStruct * imOut = ((swImageStruct *)plugin.data_out);
	unsigned char * imageIn  = (unsigned char *)imIn->buffer;
	unsigned char * imageOut = (unsigned char *)imOut->buffer;
	
	int width = imIn->width;
	int height = imIn->height;
	int depth = imIn->depth;
	
	if(!cvBlurred) 
		allocateImage(&cvBlurred, width, height);
	// Allocate cvGray
	if(!cvGray) {
		if(imIn->depth == 1) {
			cvGray = cvCreateImageHeader(cvSize(width,height), IPL_DEPTH_8U, 1);
		}
		else
			cvGray = cvCreateImage(cvSize(width,height), IPL_DEPTH_8U, 1);
	}
	
	if(imIn->depth == 1)
		cvGray->imageData = (char *)imageIn;
	else {
		if(!cvImIn)
			cvImIn = cvCreateImageHeader(cvSize(width,height), IPL_DEPTH_8U, imIn->depth);
		
		cvImIn->imageData = (char *)imageIn;
		cvCvtColor(cvImIn, cvGray, CV_BGRA2GRAY);
	}
	
	// Now we have a grayscaled image
	
	
	// First, blur the grayscaled image
	cvSmooth( cvGray, cvBlurred, 
               CV_GAUSSIAN, //int smoothtype=CV_GAUSSIAN,
               9, 9); //int param1=3, int param2=0 );
	
	// Difference to blurred
	if(!cvDiff) 
		allocateImage(&cvDiff, width, height);
	
	static unsigned long  * u32_palette = NULL;
	if(!u32_palette) {
		u32_palette = new unsigned long [257];
		for(int i=0; i<257; i++) {
			
			unsigned long r = Rthermic(i);
			unsigned long g = Gthermic(i);
			unsigned long b = Bthermic(i);
			
			u32_palette[i] = (b << 24 ) | (r << 16) | (g << 8) | (b);
		}
	}
	
	unsigned char * grayImage = (unsigned char *)cvGray->imageData;
	unsigned char * blurImage = (unsigned char *)cvBlurred->imageData;
	unsigned char * diffImage = (unsigned char *)cvDiff->imageData;
	
	unsigned long * imageOut32 = (unsigned long *)imageOut;
	for(int pos=0; pos<width*height; pos++) 
	{
		diffImage[pos] = abs((long)grayImage[pos] - (long)blurImage[pos]);
	}
	
	
	if(depth == 4) {
		for(int pos=0; pos<width*height; pos++)
			if(diffImage[pos] > 10)
				imageOut32[pos] = 0xffffffff;
			else
				imageOut32[pos] = u32_palette[ (int)diffImage[pos] ];
	} else {

		for(int pos=0; pos<width*height; pos++)
			if(diffImage[pos] > 10)
				imageOut[pos] = 0xff;
			//else
			//	imageOut[pos] = blurImage[pos];
	}
	
	
	
	// GROW REGIONS
	for(int i=0; i<min(width, height); i++) {
		
	}
	
}

void hot_pixels() {
	swImageStruct * imIn = ((swImageStruct *)plugin.data_in);
	swImageStruct * imOut = ((swImageStruct *)plugin.data_out);
	unsigned char * imageIn  = (unsigned char *)imIn->buffer;
	unsigned char * imageOut = (unsigned char *)imOut->buffer;
	
	dust_remove();
}


/*****************************************************************************/
// DO NOT MODIFY MAIN PROC
void signalhandler(int sig)
{
	fprintf(logfile, "================== RECEIVED SIGNAL %d = '%s' From process %d ==============\n", sig, sys_siglist[sig], getpid());
	signal(sig, signalhandler);
	
	if(sig != SIGUSR1)
		exit(0);
}


int main(int argc, char *argv[])
{
	// SwPluginCore load
	for(int i=0; i<NSIG; i++)
		signal(i, signalhandler);
		
	fprintf(logfile, "registerCategory...\n");
	plugin.registerCategory(CATEGORY, SUBCATEGORY);
	
	// register functions 
	fprintf(logfile, "registerFunctions...\n");
	plugin.registerFunctions(functions, nb_functions );

	// process loop
	fprintf(logfile, "loop...\n");
	plugin.loop();

	fprintf(logfile, "exit(EXIT_SUCCESS). Bye.\n");
  	return EXIT_SUCCESS;
}





#endif // PIAF_PLUGIN