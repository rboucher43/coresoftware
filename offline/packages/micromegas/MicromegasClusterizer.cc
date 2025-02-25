/*!
 * \file MicromegasClusterizer.h
 * \author Hugo Pereira Da Costa <hugo.pereira-da-costa@cea.fr>
 */

#include "MicromegasClusterizer.h"
#include "MicromegasDefs.h"
#include "CylinderGeomMicromegas.h"

#include <g4detectors/PHG4CylinderGeomContainer.h>
#include <g4detectors/PHG4CylinderGeom.h>           // for PHG4CylinderGeom

#include <trackbase/TrkrClusterContainerv4.h>        // for TrkrCluster
#include <trackbase/TrkrClusterv3.h>
#include <trackbase/TrkrDefs.h>
#include <trackbase/TrkrHitSet.h>
#include <trackbase/TrkrHit.h>
#include <trackbase/TrkrHitSetContainer.h>
#include <trackbase/TrkrClusterHitAssocv3.h>
#include <trackbase_historic/ActsTransformations.h>

#include <Acts/Definitions/Units.hpp>
#include <Acts/Surfaces/Surface.hpp>

#include <fun4all/Fun4AllReturnCodes.h>
#include <fun4all/SubsysReco.h>                     // for SubsysReco

#include <phool/getClass.h>
#include <phool/PHCompositeNode.h>
#include <phool/PHIODataNode.h>                     // for PHIODataNode
#include <phool/PHNode.h>                           // for PHNode
#include <phool/PHNodeIterator.h>                   // for PHNodeIterator
#include <phool/PHObject.h>                         // for PHObject

#include <Eigen/Dense>

#include <TVector3.h>

#include <cassert>
#include <cmath>
#include <cstdint>                                 // for uint16_t
#include <iterator>                                 // for distance
#include <map>                                      // for _Rb_tree_const_it...
#include <utility>                                  // for pair, make_pair
#include <vector>


namespace
{
  //! convenience square method
  template<class T>
    inline constexpr T square( const T& x ) { return x*x; }

  // streamers
  [[maybe_unused]] inline std::ostream& operator << (std::ostream& out, const Acts::Vector3& vector )
  {
    out << "( " << vector[0] << "," << vector[1] << "," << vector[2] << ")";
    return out;
  }

  // streamers
  [[maybe_unused]] inline std::ostream& operator << (std::ostream& out, const Acts::Vector2& vector )
  {
    out << "( " << vector[0] << "," << vector[1] << ")";
    return out;
  }

  // streamers
  [[maybe_unused]] inline std::ostream& operator << (std::ostream& out, const TVector3& vector )
  {
    out << "( " << vector.x() << "," << vector.y() << "," << vector.z() << ")";
    return out;
  }

}

//_______________________________________________________________________________
MicromegasClusterizer::MicromegasClusterizer(const std::string &name, const std::string& detector)
  : SubsysReco(name)
  , m_detector( detector )
{}

//_______________________________________________________________________________
int MicromegasClusterizer::InitRun(PHCompositeNode *topNode)
{
  PHNodeIterator iter(topNode);

  // Looking for the DST node
  auto dstNode = dynamic_cast<PHCompositeNode*>(iter.findFirst("PHCompositeNode", "DST"));
  assert( dstNode );

  // Create the Cluster node if missing
  auto trkrClusterContainer = findNode::getClass<TrkrClusterContainer>(dstNode, "TRKR_CLUSTER");
  if (!trkrClusterContainer)
  {
    PHNodeIterator dstiter(dstNode);
    auto trkrNode = dynamic_cast<PHCompositeNode *>(dstiter.findFirst("PHCompositeNode", "TRKR"));
    if(!trkrNode)
    {
      trkrNode = new PHCompositeNode("TRKR");
      dstNode->addNode(trkrNode);
    }

    trkrClusterContainer = new TrkrClusterContainerv4;
    auto TrkrClusterContainerNode = new PHIODataNode<PHObject>(trkrClusterContainer, "TRKR_CLUSTER", "PHObject");
    trkrNode->addNode(TrkrClusterContainerNode);
  }

  // create cluster to hit association node, if missing
  auto trkrClusterHitAssoc = findNode::getClass<TrkrClusterHitAssoc>(topNode,"TRKR_CLUSTERHITASSOC");
  if(!trkrClusterHitAssoc)
  {
    PHNodeIterator dstiter(dstNode);
    auto trkrNode = dynamic_cast<PHCompositeNode *>(dstiter.findFirst("PHCompositeNode", "TRKR"));
    if(!trkrNode)
    {
      trkrNode = new PHCompositeNode("TRKR");
      dstNode->addNode(trkrNode);
    }

    trkrClusterHitAssoc = new TrkrClusterHitAssocv3;
    PHIODataNode<PHObject> *newNode = new PHIODataNode<PHObject>(trkrClusterHitAssoc, "TRKR_CLUSTERHITASSOC", "PHObject");
    trkrNode->addNode(newNode);
  }
  return Fun4AllReturnCodes::EVENT_OK;
}

//_______________________________________________________________________________
int MicromegasClusterizer::process_event(PHCompositeNode *topNode)
{

  // geometry
  PHG4CylinderGeomContainer* geonode = nullptr;
  for( const std::string& geonodename: {"CYLINDERGEOM_" + m_detector + "_FULL", "CYLINDERGEOM_" + m_detector } )
  { if(( geonode =  findNode::getClass<PHG4CylinderGeomContainer>(topNode, geonodename.c_str()) )) break; }
  assert(geonode);

  // hitset container
  auto trkrhitsetcontainer = findNode::getClass<TrkrHitSetContainer>(topNode, "TRKR_HITSET");
  assert( trkrhitsetcontainer );

  // cluster container
  auto trkrClusterContainer = findNode::getClass<TrkrClusterContainer>(topNode, "TRKR_CLUSTER");
  assert( trkrClusterContainer );

  // cluster-hit association
  auto trkrClusterHitAssoc = findNode::getClass<TrkrClusterHitAssoc>(topNode, "TRKR_CLUSTERHITASSOC");
  assert( trkrClusterHitAssoc );

  // acts transformation
  ActsTransformations transform;
  
  // geometry
  auto acts_geometry = findNode::getClass<ActsTrackingGeometry>(topNode, "ActsTrackingGeometry");
  assert( acts_geometry );

  // surface map
  auto acts_surface_map = findNode::getClass<ActsSurfaceMaps>(topNode, "ActsSurfaceMaps");
  assert( acts_surface_map );

  // loop over micromegas hitsets
  const auto hitset_range = trkrhitsetcontainer->getHitSets(TrkrDefs::TrkrId::micromegasId);
  for( auto hitset_it = hitset_range.first; hitset_it != hitset_range.second; ++hitset_it )
  {
    // get hitset, key and layer
    TrkrHitSet* hitset = hitset_it->second;
    const TrkrDefs::hitsetkey hitsetkey = hitset_it->first;
    const auto layer = TrkrDefs::getLayer(hitsetkey);
    const auto tileid = MicromegasDefs::getTileId(hitsetkey);

    // get micromegas geometry object
    const auto layergeom = dynamic_cast<CylinderGeomMicromegas*>(geonode->GetLayerGeom(layer));
    assert(layergeom);

    // get micromegas acts surface
    const auto acts_surface = transform.getMMSurface( hitsetkey, acts_surface_map );
    if( !acts_surface )
    {
      std::cout
        << "MicromegasClusterizer::process_event -"
        << " could not find surface for layer " << (int) layer << " tile: " << (int) tileid
        << " skipping hitset"
        << std::endl;
      continue;
    }

    // get normal to acts surface
    const auto normal = acts_surface->normal(acts_geometry->geoContext);
   
    if( Verbosity() )
    {
      const auto geo_normal = layergeom->get_world_from_local_vect( tileid, {0, 1, 0} );
      std::cout << "MicromegasClusterizer::process_event -"
        << " layer: " << (int) layer
        << " tile: " << (int) tileid
        << " normal (acts): " << normal
        << " geo: " << geo_normal
        << std::endl;
    }

    /*
     * get segmentation type, layer thickness, strip length and pitch.
     * They are used to calculate cluster errors
     */
    const auto segmentation_type = layergeom->get_segmentation_type();
    const double thickness = layergeom->get_thickness();
    const double pitch = layergeom->get_pitch();
    const double strip_length = layergeom->get_strip_length( tileid );

    // keep a list of ranges corresponding to each cluster
    using range_list_t = std::vector<TrkrHitSet::ConstRange>;
    range_list_t ranges;

    // loop over hits
    const auto hit_range = hitset->getHits();

    // keep track of first iterator of runing cluster
    auto begin = hit_range.first;

    // keep track of previous strip
    uint16_t previous_strip = 0;
    bool first = true;

    for( auto hit_it = hit_range.first; hit_it != hit_range.second; ++hit_it )
    {

      // get hit key
      const auto hitkey = hit_it->first;

      // get strip number
      const auto strip = MicromegasDefs::getStrip( hitkey );

      if( first )
      {

        previous_strip = strip;
        first = false;
        continue;

      } else if( strip - previous_strip > 1 ) {

        // store current cluster range
        ranges.push_back( std::make_pair( begin, hit_it ) );

        // reinitialize begin of next cluster range
        begin = hit_it;

      }

      // update previous strip
      previous_strip = strip;

    }

    // store last cluster
    if( begin != hit_range.second ) ranges.push_back( std::make_pair( begin, hit_range.second ) );

    // initialize cluster count
    int cluster_count = 0;

    // loop over found hit ranges and create clusters
    for( const auto& range : ranges )
    {

      // create cluster key and corresponding cluster
      const auto ckey = TrkrDefs::genClusKey( hitsetkey, cluster_count++ );
      auto cluster = std::make_unique<TrkrClusterv3>();

      TVector3 local_coordinates;
      double weight_sum = 0;

      // needed for proper error calculation
      // it is either the sum over z, or phi, depending on segmentation
      double coord_sum = 0;
      double coordsquare_sum = 0;

      // also store adc value
      unsigned int adc_sum = 0;

      // loop over constituting hits
      for( auto hit_it = range.first; hit_it != range.second; ++hit_it )
      {
        // get hit key
        const auto hitkey = hit_it->first;
        const auto hit = hit_it->second;

        // associate cluster key to hit key
        trkrClusterHitAssoc->addAssoc(ckey, hitkey );

        // get strip number
        const auto strip = MicromegasDefs::getStrip( hitkey );

        // get adc, remove pedestal
        /* pedestal should be the same as the one used in PHG4MicromegasDigitizer */
        static constexpr double pedestal = 74.6;
        const double weight = double(hit->getAdc()) - pedestal;

        // increment cluster adc
        adc_sum += hit->getAdc();

        // get strip local coordinate and update relevant sums
        const auto strip_local_coordinate = layergeom->get_local_coordinates( tileid, strip );
        local_coordinates += strip_local_coordinate*weight;
        switch( segmentation_type )
        {
          case MicromegasDefs::SegmentationType::SEGMENTATION_PHI:
          {

            coord_sum += strip_local_coordinate.x()*weight;
            coordsquare_sum += square(strip_local_coordinate.x())*weight;
            break;
          }

          case MicromegasDefs::SegmentationType::SEGMENTATION_Z:
          {
            coord_sum += strip_local_coordinate.z()*weight;
            coordsquare_sum += square(strip_local_coordinate.z())*weight;
            break;
          }
        }

        weight_sum += weight;

      }

      local_coordinates *= (1./weight_sum);
      cluster->setAdc( adc_sum );

      // dimension and error in r, rphi and z coordinates
      static const float invsqrt12 = 1./std::sqrt(12);
      static constexpr float error_scale_phi = 1.6;
      static constexpr float error_scale_z = 0.8;

      using matrix_t = Eigen::Matrix<float, 3, 3>;
      matrix_t error = matrix_t::Zero();

      auto coord_cov = coordsquare_sum/weight_sum - square( coord_sum/weight_sum );
      auto coord_error_sq = coord_cov/weight_sum;
      switch( segmentation_type )
      {
        case MicromegasDefs::SegmentationType::SEGMENTATION_PHI:
        if( coord_error_sq == 0 ) coord_error_sq = square(pitch)/12;
        else coord_error_sq *= square(error_scale_phi);
        error(0,0) = square(thickness*invsqrt12);
        error(1,1) = coord_error_sq;
        error(2,2) = square(strip_length*invsqrt12);
        break;

        case MicromegasDefs::SegmentationType::SEGMENTATION_Z:
        if( coord_error_sq == 0 ) coord_error_sq = square(pitch)/12;
        else coord_error_sq *= square(error_scale_z);
        error(0,0) = square(thickness*invsqrt12);
        error(1,1) = square(strip_length*invsqrt12);
        error(2,2) = coord_error_sq;
        break;
      }

      /*
       * convert CylinderGeom coordinates to world
       * use acts surfaces to convert back to local coordinates,
       * this is to accomodate possible discrepencies between the two
       */
      {
        const auto world_coordinates = layergeom->get_world_from_local_coords( tileid, local_coordinates);
        const Acts::Vector3 world_coordinates_acts = {
          world_coordinates.x()*Acts::UnitConstants::cm,
          world_coordinates.y()*Acts::UnitConstants::cm,
          world_coordinates.z()*Acts::UnitConstants::cm };
        
        auto local = acts_surface->globalToLocal( acts_geometry->geoContext, world_coordinates_acts, normal );
        if( local.ok() )
        {
          const auto local_coordinates = local.value()/ Acts::UnitConstants::cm;
          cluster->setLocalX(local_coordinates.x());
          cluster->setLocalY(local_coordinates.y());
        } else {
          std::cout
            << "MicromegasClusterizer::process_event -"
            << " failed convert cluster coordinates to local surface."
            << " skipping cluster"
            << std::endl;
          continue;
        }
      } 
      
      // assign errors
      cluster->setActsLocalError(0,0, error(1,1));
      cluster->setActsLocalError(0,1, error(1,2));
      cluster->setActsLocalError(1,0, error(2,1));
      cluster->setActsLocalError(1,1,error(2,2));
      
      // add to container
      trkrClusterContainer->addClusterSpecifyKey( ckey, cluster.release() );

    }

  }
  // done
  return Fun4AllReturnCodes::EVENT_OK;
}
