// -*- mode: C++ -*-
#include <ros/ros.h>
#include <ros/names.h>

#include <pcl_ros/pcl_nodelet.h>

#include <pcl/point_types.h>
#include <pcl/impl/point_types.hpp>
#include <pcl/kdtree/kdtree_flann.h>
#include <pcl/segmentation/sac_segmentation.h>
#include <pcl/segmentation/extract_clusters.h>
#include <pcl/filters/extract_indices.h>

#include "jsk_pcl_ros/ClusterPointIndices.h"
#include "jsk_pcl_ros/EuclideanSegment.h"

using namespace std;
using namespace pcl;

namespace pcl_ros
{
  class EuclideanClustering : public PCLNodelet
  {
  public:
    EuclideanClustering()
    {};
    ~EuclideanClustering()
    {};

  private:
    ros::Publisher result_pub_;
    ros::Subscriber sub_input_;
    ros::Publisher pcl_pub_;

    ros::ServiceServer service_;

    double tolerance;
    int minsize_;
    int maxsize_;

    virtual void extract(const sensor_msgs::PointCloud2ConstPtr &input)
    {
      pcl::PointCloud<pcl::PointXYZ>::Ptr cloud (new pcl::PointCloud<pcl::PointXYZ>);
      pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_out (new pcl::PointCloud<pcl::PointXYZRGB>);

      pcl::fromROSMsg(*input, *cloud);
      pcl::fromROSMsg(*input, *cloud_out);

      ROS_INFO("subscribe input point cloud");

#if ( PCL_MAJOR_VERSION >= 1 && PCL_MINOR_VERSION >= 5 )
      pcl::search::KdTree<pcl::PointXYZ>::Ptr tree (new pcl::search::KdTree<pcl::PointXYZ>);
      tree = boost::make_shared< pcl::search::KdTree<pcl::PointXYZ> > ();
#else
      pcl::KdTree<pcl::PointXYZ>::Ptr tree (new pcl::KdTreeFLANN<pcl::PointXYZ>);
      tree = boost::make_shared<pcl::KdTreeFLANN<pcl::PointXYZ> > ();
#endif
      tree->setInputCloud (cloud);

      vector<pcl::PointIndices> cluster_indices;
      EuclideanClusterExtraction<pcl::PointXYZ> ec;
      ec.setClusterTolerance (tolerance); // 2cm
      ec.setMinClusterSize (minsize_);
      ec.setMaxClusterSize (maxsize_);
      ec.setSearchMethod (tree);
      ec.setInputCloud (cloud);
      ec.extract (cluster_indices);

      // Publish result indices
      jsk_pcl_ros::ClusterPointIndices result;
      result.cluster_indices.resize(cluster_indices.size());

      for (size_t i=0; i<cluster_indices.size(); i++)
        {
          result.cluster_indices[i].header = cluster_indices[i].header;
          result.cluster_indices[i].indices = cluster_indices[i].indices;
        }

      result_pub_.publish(result);

      // Publish point cloud after clustering
      size_t number_of_clusters = cluster_indices.size();
      for (size_t i=0; i<number_of_clusters; i++)
        {
              uint8_t r, g, b;
              r = rand()%256;
              g = rand()%256;
              b = rand()%256;
              uint32_t rgb = ((uint32_t)r<<16 | (uint32_t)g<<8 | (uint32_t)b);

          for (size_t j=0; j<cluster_indices[i].indices.size(); j++)
            {
              cloud_out->points[ cluster_indices[i].indices[j] ].rgb = *reinterpret_cast<float*>(&rgb);
            }
        }

      pcl_pub_.publish(*cloud_out);
    }

    bool serviceCallback(jsk_pcl_ros::EuclideanSegment::Request &req,
                         jsk_pcl_ros::EuclideanSegment::Response &res) {
      pcl::PointCloud<pcl::PointXYZ>::Ptr cloud (new pcl::PointCloud<pcl::PointXYZ>);
      pcl::fromROSMsg(req.input, *cloud);

      ROS_INFO("service input point cloud");

#if ( PCL_MAJOR_VERSION >= 1 && PCL_MINOR_VERSION >= 5 )
      pcl::search::KdTree<pcl::PointXYZ>::Ptr tree (new pcl::search::KdTree<pcl::PointXYZ>);
      tree = boost::make_shared< pcl::search::KdTree<pcl::PointXYZ> > ();
#else
      pcl::KdTree<pcl::PointXYZ>::Ptr tree (new pcl::KdTreeFLANN<pcl::PointXYZ>);
      tree = boost::make_shared<pcl::KdTreeFLANN<pcl::PointXYZ> > ();
#endif

      vector<pcl::PointIndices> cluster_indices;
      EuclideanClusterExtraction<pcl::PointXYZ> ec;
      double tor;
      if ( req.tolerance < 0) {
        tor = tolerance;
      } else {
        tor = req.tolerance;
      }
      ec.setClusterTolerance (tor);
      ec.setMinClusterSize (minsize_);
      ec.setMaxClusterSize (maxsize_);
      ec.setSearchMethod (tree);
      ec.setInputCloud (cloud);
      ec.extract (cluster_indices);

      ROS_INFO("clusters %d", cluster_indices.size());

      res.output.resize( cluster_indices.size() );
      pcl::ExtractIndices<sensor_msgs::PointCloud2> ex;
      ex.setInputCloud ( boost::make_shared< sensor_msgs::PointCloud2 > (req.input) );
      for ( size_t i = 0; i < cluster_indices.size(); i++ ) {
        //ex.setInputCloud ( boost::make_shared< sensor_msgs::PointCloud2 > (req.input) );
        ex.setIndices ( boost::make_shared< pcl::PointIndices > (cluster_indices[i]) );
        ex.setNegative ( false );
        ex.filter ( res.output[i] );
      }

      return true;
    }

    virtual void onInit()
    {
      // boost::shared_ptr<ros::NodeHandle> pnh_;
      // pnh_.reset (new ros::NodeHandle (getMTPrivateNodeHandle ()));

      PCLNodelet::onInit();

      pnh_->param("tolerance", tolerance, 0.02);
      ROS_INFO("tolerance : %f", tolerance);

      pnh_->param("max_size", maxsize_, 25000);
      ROS_INFO("max cluster size : %d", maxsize_);

      pnh_->param("min_size", minsize_, 20);
      ROS_INFO("min cluster size : %d", minsize_);

      result_pub_ = pnh_->advertise<jsk_pcl_ros::ClusterPointIndices> ("output", 1);
      pcl_pub_ = pnh_->advertise<pcl::PointCloud<pcl::PointXYZRGB> > ("points_output",1);
      sub_input_ = pnh_->subscribe("input", 1, &EuclideanClustering::extract, this);

      service_ = pnh_->advertiseService(pnh_->resolveName("euclidean_clustering"),
                                        &EuclideanClustering::serviceCallback, this);
    }
  };
}