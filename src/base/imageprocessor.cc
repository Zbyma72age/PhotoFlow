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

#include "imageprocessor.hh"


static gpointer run_image_processor( gpointer data )
{
  PF::ImageProcessor::Instance().run();
}


PF::ImageProcessor::ImageProcessor()
{
  processing_mutex = vips_g_mutex_new();
  requests_mutex = vips_g_mutex_new();
  requests_pending = vips_g_cond_new();
  thread = vips_g_thread_new( "image_processor", run_image_processor, NULL );
}


void PF::ImageProcessor::optimize_requests()
{
  for( std::deque<ProcessRequestInfo>::iterator i = requests.begin();
       i != requests.end(); i++ ) {
    if( i->request != IMAGE_REBUILD ) continue;
    i++;
    requests.erase( i, requests.end() );
    break;
  }
}


void PF::ImageProcessor::run()
{
	bool running = true;
  while( running ) {
    g_mutex_lock( requests_mutex );
    if( requests.empty() ) {      
      g_mutex_unlock( requests_mutex );
      PF::Image* image = PF::PhotoFlow::Instance().get_active_image();
      if( image ) {
        PF::CacheBuffer* buf = image->get_layer_manager().get_cache_buffer();
        //std::cout<<"ImageProcessor::run(): buf="<<buf<<std::endl;
        if( buf ) {
          buf->step();
          if( buf->is_completed() ) {
            image->lock();
            image->do_update();
            image->unlock();
          }
          continue;
        }
      }
      std::cout<<"ImageProcessor::run(): waiting for new requests..."<<std::endl;
      g_cond_wait( requests_pending, requests_mutex );
      //std::cout<<"PF::ImageProcessor::run(): resuming."<<std::endl;
      if( requests.empty() ) {
				std::cout<<"PF::ImageProcessor::run(): WARNING: empty requests queue after resuming!!!"<<std::endl;
				g_mutex_unlock( requests_mutex );
				continue;
      }
    }
    optimize_requests();
    PF::ProcessRequestInfo request = requests.front();
    requests.pop_front();
    /*
    std::cout<<"PF::ImageProcessor::run(): processing new request: ";
    switch( request.request ) {
    case IMAGE_REBUILD: std::cout<<"IMAGE_REBUILD"; break;
    case IMAGE_REDRAW_START: std::cout<<"IMAGE_REDRAW_START"; break;
    case IMAGE_REDRAW_END: std::cout<<"IMAGE_REDRAW_END"; break;
    case IMAGE_REDRAW: std::cout<<"IMAGE_REDRAW"; break;
    default: break;
    }
    std::cout<<std::endl;
    */
    g_mutex_unlock( requests_mutex );


    // Process the request
    switch( request.request ) {
    case IMAGE_REBUILD:
      if( !request.image ) continue;
      //std::cout<<"PF::ImageProcessor::run(): locking image..."<<std::endl;
      request.image->lock();
      //std::cout<<"PF::ImageProcessor::run(): image locked."<<std::endl;
			/*
			if( (request.area.width!=0) && (request.area.height!=0) )
				request.image->do_update( &(request.area) );
			else
				request.image->do_update( NULL );
			*/
			request.image->do_update( request.pipeline );
      request.image->unlock();
      request.image->rebuild_done_signal();
      //std::cout<<"PF::ImageProcessor::run(): updating image done."<<std::endl;
      break;
    case IMAGE_SAMPLE:
      if( !request.image ) continue;
      //std::cout<<"PF::ImageProcessor::run(): locking image..."<<std::endl;
      request.image->sample_lock();
      //std::cout<<"PF::ImageProcessor::run(IMAGE_SAMPLE): image locked."<<std::endl;
			if( (request.area.width!=0) && (request.area.height!=0) )
				request.image->do_sample( request.layer_id, request.area );
      request.image->sample_unlock();
      request.image->sample_done_signal();
      //std::cout<<"PF::ImageProcessor::run(IMAGE_SAMPLE): sampling done."<<std::endl;
      break;
		case IMAGE_UPDATE:
      if( !request.pipeline ) continue;
      //std::cout<<"PF::ImageProcessor::run(): updating area."<<std::endl;
      request.pipeline->sink( request.area );
      //std::cout<<"PF::ImageProcessor::run(): updating area done."<<std::endl;
      break;
    case IMAGE_REDRAW_START:
      request.sink->process_start( request.area );
      break;
    case IMAGE_REDRAW_END:
      request.sink->process_end( request.area );
      break;
    case IMAGE_REDRAW:
      // Get exclusive access to the request data structure
      //g_mutex_lock( request.mutex );
      if( !request.sink ) continue;

      //std::cout<<"PF::ImageProcessor::run(): processing area "
      //	       <<request.area.width<<","<<request.area.height
      //       <<"+"<<request.area.left<<"+"<<request.area.top
      //       <<std::endl;
      // Process the requested image portion
      request.sink->process_area( request.area );
      //std::cout<<"PF::ImageProcessor::run(): processing area done."<<std::endl;

      // Notify that the processing is finished
      //g_cond_signal( request.done );
      //g_mutex_unlock( request.mutex );
      break;
    case IMAGE_REMOVE_LAYER:
      if( !request.image ) break;
      if( !request.layer ) break;
      request.image->remove_layer_lock();
      request.image->do_remove_layer( request.layer );
      request.image->remove_layer_unlock();
      request.image->remove_layer_done_signal();
      break;
    case IMAGE_DESTROY:
      if( !request.image ) continue;
      delete request.image;
      std::cout<<"PF::ImageProcessor::run(): image destroyed."<<std::endl;
      break;
    case PROCESSOR_END:
			running = false;
      std::cout<<"PF::ImageProcessor::run(): processing ended."<<std::endl;
      break;
    default:
      break;
    }
  }
}


void  PF::ImageProcessor::submit_request( PF::ProcessRequestInfo request )
{
  //std::cout<<"PF::ImageProcessor::submit_request(): locking mutex."<<std::endl;
  g_mutex_lock( requests_mutex );
  //std::cout<<"PF::ImageProcessor::submit_request(): pushing request."<<std::endl;
  requests.push_back( request );
  //std::cout<<"PF::ImageProcessor::submit_request(): unlocking mutex."<<std::endl;
  g_mutex_unlock( requests_mutex );
  //std::cout<<"PF::ImageProcessor::submit_request(): signaling condition."<<std::endl;
  g_cond_signal( requests_pending );
}


PF::ImageProcessor* PF::ImageProcessor::instance = NULL;

PF::ImageProcessor& PF::ImageProcessor::Instance() { 
  if(!PF::ImageProcessor::instance) 
    PF::ImageProcessor::instance = new PF::ImageProcessor();
  return( *instance );
};
