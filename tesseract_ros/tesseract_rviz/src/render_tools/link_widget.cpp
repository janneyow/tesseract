/*
 * Copyright (c) 2008, Willow Garage, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Willow Garage, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <tesseract_environment/core/macros.h>
TESSERACT_ENVIRONMENT_IGNORE_WARNINGS_PUSH
#include <boost/filesystem.hpp>

#include <OgreEntity.h>
#include <OgreManualObject.h>
#include <OgreMaterial.h>
#include <OgreMaterialManager.h>
#include <OgreMesh.h>
#include <OgreRibbonTrail.h>
#include <OgreSceneManager.h>
#include <OgreSceneNode.h>
#include <OgreSharedPtr.h>
#include <OgreSubEntity.h>
#include <OgreTechnique.h>
#include <OgreTextureManager.h>
#include <OgreNameGenerator.h>

#include <ros/console.h>

#include <resource_retriever/retriever.h>
#include <octomap/octomap.h>
#include <octomap_msgs/Octomap.h>

#include "rviz/load_resource.h"
#include "rviz/mesh_loader.h"
#include "rviz/ogre_helpers/axes.h"
#include "rviz/ogre_helpers/mesh_shape.h"
#include "rviz/ogre_helpers/object.h"
#include "rviz/ogre_helpers/shape.h"
#include "rviz/properties/bool_property.h"
#include "rviz/properties/float_property.h"
#include "rviz/properties/property.h"
#include "rviz/properties/quaternion_property.h"
#include "rviz/properties/vector_property.h"
#include "rviz/properties/string_property.h"
#include "rviz/selection/selection_manager.h"
#include "rviz/visualization_manager.h"
#include "rviz/ogre_helpers/point_cloud.h"
TESSERACT_ENVIRONMENT_IGNORE_WARNINGS_POP

#include "tesseract_rviz/render_tools/env_visualization.h"
#include "tesseract_rviz/render_tools/env_joint.h"
#include "tesseract_rviz/render_tools/env_link.h"

namespace fs = boost::filesystem;

namespace tesseract_rviz
{
static Ogre::NameGenerator link_name_generator("Tesseract_Link");
static Ogre::NameGenerator clone_link_name_generator("Tesseract_Link_Clone");
static Ogre::NameGenerator material_name_generator("Tesseract_Material");
static Ogre::NameGenerator trail_name_generator("Tesseract_Trail");
static Ogre::NameGenerator point_cloud_name_generator("Tesseract_PointCloud");

class EnvLinkSelectionHandler : public rviz::SelectionHandler
{
public:
  EnvLinkSelectionHandler(EnvLink* link, rviz::DisplayContext* context);
  ~EnvLinkSelectionHandler() override;

  void createProperties(const rviz::Picked& /*obj*/, rviz::Property* parent_property) override;
  void updateProperties() override;

  void preRenderPass(uint32_t /*pass*/) override;
  void postRenderPass(uint32_t /*pass*/) override;

private:
  EnvLink* link_;
  rviz::VectorProperty* position_property_;
  rviz::QuaternionProperty* orientation_property_;
};

EnvLinkSelectionHandler::EnvLinkSelectionHandler(EnvLink* link, rviz::DisplayContext* context)
  : rviz::SelectionHandler(context), link_(link)
{
}

EnvLinkSelectionHandler::~EnvLinkSelectionHandler() {}
void EnvLinkSelectionHandler::createProperties(const rviz::Picked& /*obj*/, rviz::Property* parent_property)
{
  rviz::Property* group =
      new rviz::Property("Link " + QString::fromStdString(link_->getName()), QVariant(), "", parent_property);
  properties_.push_back(group);

  position_property_ = new rviz::VectorProperty("Position", Ogre::Vector3::ZERO, "", group);
  position_property_->setReadOnly(true);

  orientation_property_ = new rviz::QuaternionProperty("Orientation", Ogre::Quaternion::IDENTITY, "", group);
  orientation_property_->setReadOnly(true);

  group->expand();
}

void EnvLinkSelectionHandler::updateProperties()
{
  position_property_->setVector(link_->getPosition());
  orientation_property_->setQuaternion(link_->getOrientation());
}

void EnvLinkSelectionHandler::preRenderPass(uint32_t /*pass*/)
{
  if (!link_->is_selectable_)
  {
    if (link_->visual_node_)
    {
      link_->visual_node_->setVisible(false);
    }
    if (link_->collision_node_)
    {
      link_->collision_node_->setVisible(false);
    }

    if (link_->visual_trajectory_node_)
    {
      link_->visual_trajectory_node_->setVisible(false);
    }
    if (link_->collision_trajectory_node_)
    {
      link_->collision_trajectory_node_->setVisible(false);
    }

    if (link_->trail_)
    {
      link_->trail_->setVisible(false);
    }
    if (link_->axes_)
    {
      link_->axes_->getSceneNode()->setVisible(false);
    }
  }
}

void EnvLinkSelectionHandler::postRenderPass(uint32_t /*pass*/)
{
  if (!link_->is_selectable_)
  {
    link_->updateVisibility();
  }
}

EnvLink::EnvLink(EnvVisualization* env,
                 const tesseract_scene_graph::Link& link,
                 bool visual,
                 bool collision)
  : env_(env)
  , scene_manager_(env->getDisplayContext()->getSceneManager())
  , context_(env->getDisplayContext())
  , name_(link.getName())
  , visual_node_(nullptr)
  , collision_node_(nullptr)
  , trail_(nullptr)
  , axes_(nullptr)
  , material_alpha_(1.0)
  , alpha_(1.0)
  , only_render_depth_(false)
  , is_selectable_(true)
  , using_color_(false)
{
  link_property_ = new rviz::Property(link.getName().c_str(), true, "", nullptr, SLOT(updateVisibility()), this);
  link_property_->setIcon(rviz::loadPixmap("package://rviz/icons/classes/RobotLink.png"));

  details_ = new rviz::Property("Details", QVariant(), "", nullptr);

  alpha_property_ = new rviz::FloatProperty(
      "Alpha", 1, "Amount of transparency to apply to this link.", link_property_, SLOT(updateAlpha()), this);

  trail_property_ = new rviz::Property("Show Trail",
                                       false,
                                       "Enable/disable a 2 meter \"ribbon\" which follows this link.",
                                       link_property_,
                                       SLOT(updateTrail()),
                                       this);

  axes_property_ = new rviz::Property(
      "Show Axes", false, "Enable/disable showing the axes of this link.", link_property_, SLOT(updateAxes()), this);

  position_property_ = new rviz::VectorProperty("Position",
                                                Ogre::Vector3::ZERO,
                                                "Position of this link, in the current Fixed Frame.  (Not editable)",
                                                link_property_);
  position_property_->setReadOnly(true);

  orientation_property_ =
      new rviz::QuaternionProperty("Orientation",
                                   Ogre::Quaternion::IDENTITY,
                                   "Orientation of this link, in the current Fixed Frame.  (Not editable)",
                                   link_property_);
  orientation_property_->setReadOnly(true);

  collision_enabled_property_ = new rviz::StringProperty("Collision",
                                                         "enabled",
                                                         "Indicate if link is considered during collision checking.",
                                                         link_property_);
  collision_enabled_property_->setReadOnly(true);


  allowed_collision_matrix_property_ = new rviz::Property("ACM", "", "Links allowed to be in collision with", collision_enabled_property_);

  allowed_collision_matrix_property_->setReadOnly(true);

  link_property_->collapse();

  visual_node_ = env_->getVisualNode()->createChildSceneNode();
  collision_node_ = env_->getCollisionNode()->createChildSceneNode();

  visual_trajectory_node_ = env_->getVisualNode()->createChildSceneNode();
  collision_trajectory_node_ = env_->getCollisionNode()->createChildSceneNode();

  // create material for coloring links
  color_material_ = Ogre::MaterialManager::getSingleton().create(material_name_generator.generate(), "rviz");
  color_material_->setReceiveShadows(false);
  color_material_->getTechnique(0)->setLightingEnabled(true);

  // create the ogre objects to display

  if (visual)
  {
    createVisual(link);
  }

  if (collision)
  {
    createCollision(link);
  }

  if (collision || visual)
  {
    createSelection();
  }

  if (!hasGeometry())
  {
    link_property_->setIcon(rviz::loadPixmap("package://rviz/icons/classes/RobotLinkNoGeom.png"));
    alpha_property_->hide();
    collision_enabled_property_->hide();
    allowed_collision_matrix_property_->hide();
    link_property_->setValue(QVariant());
  }
}

void EnvLink::setLinkPropertyDescription()
{
  // create description and fill in child_joint_names_ vector
  std::stringstream desc;
  EnvJoint* parent_joint = env_->findParentJoint(this);
  if (parent_joint == nullptr)
  {
    if (this == env_->getRootLink())
      desc << "Root Link <b>" << name_ << "</b>";
    else
      desc << "Floating Link <b>" << name_ << "</b>";
  }
  else
  {
    desc << "Link <b>" << name_ << "</b>";
    desc << " with parent joint <b>" << parent_joint->getName() << "</b>";
  }

  EnvJoint* child_joint = env_->findChildJoint(this);
  if (child_joint == nullptr)
  {
    desc << " has no children.";
  }
  else
  {
    int count = 1;
    std::string child_joints_desc = "<b>" + child_joint->getName() + "</b>";
    child_joint = env_->findChildJoint(env_->getLink(child_joint->getChildLinkName()));
    while (child_joint != nullptr)
    {
      ++count;
      child_joints_desc += ", <b>" + child_joint->getName() + "</b>";
      child_joint = env_->findChildJoint(env_->getLink(child_joint->getChildLinkName()));
    }
    child_joints_desc += ".";

    desc << " has " << count;

    if (count > 1)
    {
      desc << " child joints: ";
    }
    else
    {
      desc << " child joint: ";
    }
    desc << child_joints_desc;
  }
  if (hasGeometry())
  {
    desc << "  Check/uncheck to show/hide this link in the display.";
    if (visual_meshes_.empty())
    {
      desc << "  This link has collision geometry but no visible geometry.";
    }
    else if (collision_meshes_.empty())
    {
      desc << "  This link has visible geometry but no collision geometry.";
    }
  }
  else
  {
    desc << "  This link has NO geometry.";
  }

  link_property_->setDescription(desc.str().c_str());
}

EnvLink::~EnvLink()
{
  for (size_t i = 0; i < visual_meshes_.size(); i++)
  {
    scene_manager_->destroyEntity(visual_meshes_[i]);
  }

  for (size_t i = 0; i < collision_meshes_.size(); i++)
  {
    scene_manager_->destroyEntity(collision_meshes_[i]);
  }

  for (size_t i = 0; i < visual_trajectory_meshes_.size(); i++)
  {
    scene_manager_->destroyEntity(visual_trajectory_meshes_[i]);
  }

  for (size_t i = 0; i < collision_trajectory_meshes_.size(); i++)
  {
    scene_manager_->destroyEntity(collision_trajectory_meshes_[i]);
  }

  for (size_t i = 0; i < visual_octrees_.size(); i++)
  {
    //    scene_manager_->destroyMovableObject( octree_objects_[ i ]); TODO:
    //    Need to create a MovableObjectFactory for this type.
    delete visual_octrees_[i];
  }
  visual_octrees_.clear();

  for (size_t i = 0; i < collision_octrees_.size(); i++)
  {
    //    scene_manager_->destroyMovableObject( octree_objects_[ i ]); TODO:
    //    Need to create a MovableObjectFactory for this type.
    delete collision_octrees_[i];
  }
  collision_octrees_.clear();

  scene_manager_->destroySceneNode(visual_node_);
  scene_manager_->destroySceneNode(collision_node_);

  scene_manager_->destroySceneNode(visual_trajectory_node_);
  scene_manager_->destroySceneNode(collision_trajectory_node_);



  if (trail_)
  {
    scene_manager_->destroyRibbonTrail(trail_);
  }

  delete axes_;
  delete details_;
  delete link_property_;
}

bool EnvLink::hasGeometry() const
{
  return visual_meshes_.size() + collision_meshes_.size() + visual_octrees_.size() + collision_octrees_.size() > 0;
}

bool EnvLink::getEnabled() const
{
  if (!hasGeometry())
    return true;
  return link_property_->getValue().toBool();
}

void EnvLink::setAlpha(float a)
{
  alpha_ = a;
  updateAlpha();
}

void EnvLink::setRenderQueueGroup(Ogre::uint8 group)
{
  Ogre::SceneNode::ChildNodeIterator child_it = visual_node_->getChildIterator();
  while (child_it.hasMoreElements())
  {
    Ogre::SceneNode* child = dynamic_cast<Ogre::SceneNode*>(child_it.getNext());
    if (child)
    {
      Ogre::SceneNode::ObjectIterator object_it = child->getAttachedObjectIterator();
      while (object_it.hasMoreElements())
      {
        Ogre::MovableObject* obj = object_it.getNext();
        obj->setRenderQueueGroup(group);
      }
    }
  }
}

void EnvLink::setOnlyRenderDepth(bool onlyRenderDepth)
{
  setRenderQueueGroup(onlyRenderDepth ? Ogre::RENDER_QUEUE_BACKGROUND : Ogre::RENDER_QUEUE_MAIN);
  only_render_depth_ = onlyRenderDepth;
  updateAlpha();
}

void EnvLink::updateAlpha()
{
  float link_alpha = alpha_property_->getFloat();
  M_SubEntityToMaterial::iterator it = materials_.begin();
  M_SubEntityToMaterial::iterator end = materials_.end();
  for (; it != end; ++it)
  {
    const Ogre::MaterialPtr& material = it->second;

    if (only_render_depth_)
    {
      material->setColourWriteEnabled(false);
      material->setDepthWriteEnabled(true);
    }
    else
    {
      Ogre::ColourValue color = material->getTechnique(0)->getPass(0)->getDiffuse();
      color.a = alpha_ * material_alpha_ * link_alpha;
      material->setDiffuse(color);

      if (color.a < 0.9998)
      {
        material->setSceneBlending(Ogre::SBT_TRANSPARENT_ALPHA);
        material->setDepthWriteEnabled(false);
      }
      else
      {
        material->setSceneBlending(Ogre::SBT_REPLACE);
        material->setDepthWriteEnabled(true);
      }
    }
  }

  Ogre::ColourValue color = color_material_->getTechnique(0)->getPass(0)->getDiffuse();
  color.a = alpha_ * link_alpha;
  color_material_->setDiffuse(color);

  if (color.a < 0.9998)
  {
    color_material_->setSceneBlending(Ogre::SBT_TRANSPARENT_ALPHA);
    color_material_->setDepthWriteEnabled(false);
  }
  else
  {
    color_material_->setSceneBlending(Ogre::SBT_REPLACE);
    color_material_->setDepthWriteEnabled(true);
  }

  for (const auto& octree : visual_octrees_)
  {
    octree->setAlpha(alpha_ * link_alpha);
  }

  for (const auto& octree : collision_octrees_)
  {
    octree->setAlpha(alpha_ * link_alpha);
  }
}

void EnvLink::updateVisibility()
{
  bool enabled = getEnabled();

  env_->calculateJointCheckboxes();

  if (visual_node_)
  {
    visual_node_->setVisible(enabled && env_->isVisible() && env_->isVisualVisible());
  }
  if (collision_node_)
  {
    collision_node_->setVisible(enabled && env_->isVisible() && env_->isCollisionVisible());
  }

  if (visual_trajectory_node_)
  {
    visual_trajectory_node_->setVisible(enabled && env_->isVisible() && env_->isVisualVisible());
  }
  if (collision_trajectory_node_)
  {
    collision_trajectory_node_->setVisible(enabled && env_->isVisible() && env_->isCollisionVisible());
  }

  if (trail_)
  {
    trail_->setVisible(enabled && env_->isVisible());
  }
  if (axes_)
  {
    axes_->getSceneNode()->setVisible(enabled && env_->isVisible());
  }
}

Ogre::MaterialPtr EnvLink::getMaterialForLink(const tesseract_scene_graph::Link& link, const std::string material_name)
{
  if (link.visual.empty() || !link.visual[0]->material)
  {
    return Ogre::MaterialManager::getSingleton().getByName("RVIZ/ShadedRed");
  }

  Ogre::MaterialPtr mat = Ogre::MaterialManager::getSingleton().create(material_name_generator.generate(), "rviz");
  mat->getTechnique(0)->setLightingEnabled(true);

  tesseract_scene_graph::VisualPtr visual = nullptr;
  std::vector<tesseract_scene_graph::VisualPtr>::const_iterator vi;
  for (vi = link.visual.begin(); vi != link.visual.end(); vi++)
  {
    if ((*vi) && material_name != "" && (*vi)->material_name == material_name)
    {
      visual = *vi;
      break;
    }
  }

  if (visual->material->texture_filename.empty())
  {
    const Eigen::Vector4d& col = visual->material->color;
    mat->getTechnique(0)->setAmbient(col(0) * 0.5f, col(1) * 0.5f, col(2) * 0.5f);
    mat->getTechnique(0)->setDiffuse(col(0), col(1), col(2), col(3));

    material_alpha_ = col(3);
  }
  else
  {
    std::string filename = visual->material->texture_filename;
    if (!Ogre::TextureManager::getSingleton().resourceExists(filename))
    {
      resource_retriever::Retriever retriever;
      resource_retriever::MemoryResource res;
      try
      {
        res = retriever.get(filename);
      }
      catch (resource_retriever::Exception& e)
      {
        ROS_ERROR("%s", e.what());
      }

      if (res.size != 0)
      {
        Ogre::DataStreamPtr stream(new Ogre::MemoryDataStream(res.data.get(), res.size));
        Ogre::Image image;
        std::string extension = fs::extension(fs::path(filename));

        if (extension[0] == '.')
        {
          extension = extension.substr(1, extension.size() - 1);
        }

        try
        {
          image.load(stream, extension);
          Ogre::TextureManager::getSingleton().loadImage(
              filename, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, image);
        }
        catch (Ogre::Exception& e)
        {
          ROS_ERROR("Could not load texture [%s]: %s", filename.c_str(), e.what());
        }
      }
    }

    Ogre::Pass* pass = mat->getTechnique(0)->getPass(0);
    Ogre::TextureUnitState* tex_unit = pass->createTextureUnitState();
    ;
    tex_unit->setTextureName(filename);
  }

  return mat;
}

bool EnvLink::createEntityForGeometryElement(const tesseract_scene_graph::Link& link,
                                             const tesseract_geometry::Geometry& geom,
                                             const Eigen::Isometry3d& origin,
                                             const std::string& material_name,
                                             bool isVisual)
{
  Ogre::Entity* entity = nullptr;  // default in case nothing works.

  std::string entity_name = link_name_generator.generate();

  Ogre::Vector3 scale(Ogre::Vector3::UNIT_SCALE);

  Ogre::Vector3 offset_position(Ogre::Vector3::ZERO);
  Ogre::Quaternion offset_orientation(Ogre::Quaternion::IDENTITY);

  {
    const Eigen::Vector3d& pos = origin.translation();
    Ogre::Vector3 position(static_cast<float>(pos(0)),
                           static_cast<float>(pos(1)),
                           static_cast<float>(pos(2)));

    Eigen::Quaterniond rot(origin.linear());
    Ogre::Quaternion orientation(Ogre::Quaternion::IDENTITY);
    orientation = orientation * Ogre::Quaternion(static_cast<float>(rot.w()),
                                                 static_cast<float>(rot.x()),
                                                 static_cast<float>(rot.y()),
                                                 static_cast<float>(rot.z()));

    offset_position = position;
    offset_orientation = orientation;
  }

  switch (geom.getType())
  {
    case tesseract_geometry::GeometryType::SPHERE:
    {
      const tesseract_geometry::Sphere& sphere = static_cast<const tesseract_geometry::Sphere&>(geom);
      entity = rviz::Shape::createEntity(entity_name, rviz::Shape::Sphere, scene_manager_);
      float diameter = static_cast<float>(sphere.getRadius()) * 2.0f;
      scale = Ogre::Vector3(diameter, diameter, diameter);
      break;
    }
    case tesseract_geometry::GeometryType::BOX:
    {
      const tesseract_geometry::Box& box = static_cast<const tesseract_geometry::Box&>(geom);
      entity = rviz::Shape::createEntity(entity_name, rviz::Shape::Cube, scene_manager_);

      scale =
          Ogre::Vector3(static_cast<float>(box.getX()), static_cast<float>(box.getY()), static_cast<float>(box.getZ()));

      break;
    }
    case tesseract_geometry::GeometryType::CYLINDER:
    {
      const tesseract_geometry::Cylinder& cylinder = static_cast<const tesseract_geometry::Cylinder&>(geom);

      Ogre::Quaternion rotX;
      rotX.FromAngleAxis(Ogre::Degree(90), Ogre::Vector3::UNIT_X);
      offset_orientation = offset_orientation * rotX;

      entity = rviz::Shape::createEntity(entity_name, rviz::Shape::Cylinder, scene_manager_);
      scale = Ogre::Vector3(static_cast<float>(cylinder.getRadius() * 2),
                            static_cast<float>(cylinder.getLength()),
                            static_cast<float>(cylinder.getRadius() * 2));
      break;
    }
    case tesseract_geometry::GeometryType::MESH:
    {
      const tesseract_geometry::Mesh& mesh = static_cast<const tesseract_geometry::Mesh&>(geom);

      if (mesh.getFilePath().empty())
        return false;

//      scale = Ogre::Vector3(
//          static_cast<float>(mesh.scale.x), static_cast<float>(mesh.scale.y), static_cast<float>(mesh.scale.z));

      std::string model_name = "file://" + mesh.getFilePath();

      try
      {
        rviz::loadMeshFromResource(model_name);
        entity = scene_manager_->createEntity(entity_name, model_name);
      }
      catch (Ogre::InvalidParametersException& e)
      {
        ROS_ERROR("Could not convert mesh resource '%s' for link '%s'. It might "
                  "be an empty mesh: %s",
                  model_name.c_str(),
                  link.getName().c_str(),
                  e.what());
      }
      catch (Ogre::Exception& e)
      {
        ROS_ERROR("Could not load model '%s' for link '%s': %s", model_name.c_str(), link.getName().c_str(), e.what());
      }
      break;
    }
    case tesseract_geometry::GeometryType::CONVEX_MESH:
    {
      const tesseract_geometry::ConvexMesh& mesh = static_cast<const tesseract_geometry::ConvexMesh&>(geom);

      if (mesh.getFilePath().empty())
        return false;

  //      scale = Ogre::Vector3(
  //          static_cast<float>(mesh.scale.x), static_cast<float>(mesh.scale.y), static_cast<float>(mesh.scale.z));

      std::string model_name = "file://" + mesh.getFilePath();

      try
      {
        rviz::loadMeshFromResource(model_name);
        entity = scene_manager_->createEntity(entity_name, model_name);
      }
      catch (Ogre::InvalidParametersException& e)
      {
        ROS_ERROR("Could not convert mesh resource '%s' for link '%s'. It might "
                  "be an empty mesh: %s",
                  model_name.c_str(),
                  link.getName().c_str(),
                  e.what());
      }
      catch (Ogre::Exception& e)
      {
        ROS_ERROR("Could not load model '%s' for link '%s': %s", model_name.c_str(), link.getName().c_str(), e.what());
      }
      break;
    }
    case tesseract_geometry::GeometryType::OCTREE:
    {
      std::size_t max_octree_depth = 0;
      double color_factor = 0.8;
      OctreeVoxelRenderMode octree_voxel_rendering = OCTOMAP_OCCUPIED_VOXELS;
      OctreeVoxelColorMode octree_color_mode = OCTOMAP_Z_AXIS_COLOR;
      std::size_t octree_depth;
      Ogre::SceneNode* offset_node;
      std::vector<rviz::PointCloud*>* octree_objects;

      const std::shared_ptr<const octomap::OcTree>& octree = static_cast<const tesseract_geometry::Octree&>(geom).getOctree();

      if (!max_octree_depth)
        octree_depth = octree->getTreeDepth();
      else
        octree_depth = std::min(max_octree_depth, (std::size_t)octree->getTreeDepth());

      if (isVisual)
      {
        offset_node = visual_node_->createChildSceneNode();
        octree_objects = &visual_octrees_;
      }
      else
      {
        offset_node = collision_node_->createChildSceneNode();
        octree_objects = &collision_octrees_;
      }

      std::vector<std::vector<rviz::PointCloud::Point>> pointBuf;
      pointBuf.resize(octree_depth);

      // get dimensions of octree
      double minX, minY, minZ, maxX, maxY, maxZ;
      octree->getMetricMin(minX, minY, minZ);
      octree->getMetricMax(maxX, maxY, maxZ);

      unsigned int render_mode_mask = static_cast<unsigned int>(octree_voxel_rendering);

      size_t pointCount = 0;
      {
        // traverse all leafs in the tree:
        for (octomap::OcTree::iterator it = octree->begin(static_cast<unsigned char>(octree_depth)), end = octree->end();
             it != end;
             ++it)
        {
          bool display_voxel = false;

          // the left part evaluates to 1 for free voxels and 2 for occupied
          // voxels
          if ((static_cast<unsigned>(octree->isNodeOccupied(*it)) + 1) & render_mode_mask)
          {
            // check if current voxel has neighbors on all sides -> no need to be
            // displayed
            bool allNeighborsFound = true;

            octomap::OcTreeKey key;
            octomap::OcTreeKey nKey = it.getKey();

            for (key[2] = static_cast<octomap::key_type>(nKey[2] - 1);
                 allNeighborsFound && key[2] <= static_cast<octomap::key_type>(nKey[2] + 1);
                 ++key[2])
            {
              for (key[1] = static_cast<octomap::key_type>(nKey[1] - 1);
                   allNeighborsFound && key[1] <= static_cast<octomap::key_type>(nKey[1] + 1);
                   ++key[1])
              {
                for (key[0] = static_cast<octomap::key_type>(nKey[0] - 1);
                     allNeighborsFound && key[0] <= static_cast<octomap::key_type>(nKey[0] + 1);
                     ++key[0])
                {
                  if (key != nKey)
                  {
                    octomap::OcTreeNode* node = octree->search(key);

                    // the left part evaluates to 1 for free voxels and 2 for
                    // occupied voxels
                    if (!(node && (static_cast<unsigned>(octree->isNodeOccupied(node)) + 1) & render_mode_mask))
                    {
                      // we do not have a neighbor => break!
                      allNeighborsFound = false;
                    }
                  }
                }
              }
            }

            display_voxel |= !allNeighborsFound;
          }

          if (display_voxel)
          {
            rviz::PointCloud::Point newPoint;

            newPoint.position.x = static_cast<float>(it.getX());
            newPoint.position.y = static_cast<float>(it.getY());
            newPoint.position.z = static_cast<float>(it.getZ());

            float cell_probability;

            switch (octree_color_mode)
            {
              case OCTOMAP_Z_AXIS_COLOR:
                setOctomapColor(newPoint.position.z, minZ, maxZ, color_factor, &newPoint);
                break;
              case OCTOMAP_PROBABLILTY_COLOR:
                cell_probability = static_cast<float>(it->getOccupancy());
                newPoint.setColor((1.0f - cell_probability), cell_probability, 0.0f);
                break;
              default:
                break;
            }

            // push to point vectors
            unsigned int depth = it.getDepth();
            pointBuf[depth - 1].push_back(newPoint);

            ++pointCount;
          }
        }
      }

      for (unsigned i = 0; i < octree_depth; ++i)
      {
        float size = static_cast<float>(octree->getNodeSize(static_cast<unsigned>(i + 1)));

        rviz::PointCloud* cloud = new rviz::PointCloud();
        cloud->setName(point_cloud_name_generator.generate());
        cloud->setRenderMode(rviz::PointCloud::RM_BOXES);
        cloud->clear();
        cloud->setDimensions(size, size, size);

        cloud->addPoints(&pointBuf[i].front(), static_cast<unsigned>(pointBuf[i].size()));
        pointBuf[i].clear();

        offset_node->attachObject(cloud);
        octree_objects->push_back(cloud);
      }

      offset_node->setScale(scale);
      offset_node->setPosition(offset_position);
      offset_node->setOrientation(offset_orientation);

      return true;
    }
    default:
      ROS_WARN("Unsupported geometry type for element: %d", geom.getType());
      break;
  }

  if (entity)
  {
    Ogre::SceneNode* offset_node;
    std::vector<Ogre::Entity*>* meshes;
    if (isVisual)
    {
      offset_node = visual_node_->createChildSceneNode();
      meshes = &visual_meshes_;
    }
    else
    {
      offset_node = collision_node_->createChildSceneNode();
      meshes = &collision_meshes_;
    }

    offset_node->attachObject(entity);
    offset_node->setScale(scale);
    offset_node->setPosition(offset_position);
    offset_node->setOrientation(offset_orientation);

    if (default_material_name_.empty())
    {
      default_material_ = getMaterialForLink(link);
      default_material_ = default_material_->clone(material_name_generator.generate());
      default_material_name_ = default_material_->getName();
    }

    for (uint32_t i = 0; i < entity->getNumSubEntities(); ++i)
    {
      default_material_ = getMaterialForLink(link, material_name);
      default_material_ = default_material_->clone(material_name_generator.generate());
      default_material_name_ = default_material_->getName();

      // Assign materials only if the submesh does not have one already

      Ogre::SubEntity* sub = entity->getSubEntity(i);
      const std::string& material_name = sub->getMaterialName();

      if (material_name == "BaseWhite" || material_name == "BaseWhiteNoLighting")
      {
        sub->setMaterialName(default_material_name_);
      }
      else
      {
        std::string cloned_name = material_name_generator.generate();
        sub->getMaterial()->clone(cloned_name);
        sub->setMaterialName(cloned_name);
      }

      materials_[sub] = sub->getMaterial();
    }

    meshes->push_back(entity);
    return true;
  }

  return false;
}

Ogre::SceneNode* EnvLink::clone(Ogre::SceneNode* scene_node, bool isVisual)
{
  Ogre::SceneNode* cloned_scene_node;
  if (isVisual)
    cloned_scene_node = visual_trajectory_node_->createChildSceneNode();
  else
    cloned_scene_node = collision_trajectory_node_->createChildSceneNode();

  Ogre::SceneNode::ObjectIterator iter = scene_node->getAttachedObjectIterator();
  while (iter.hasMoreElements())
  {
    Ogre::Entity* entity = static_cast<Ogre::Entity*>(iter.getNext())->clone(clone_link_name_generator.generate());

    if (isVisual)
      visual_trajectory_meshes_.push_back(entity);
    else
      collision_trajectory_meshes_.push_back(entity);

    cloned_scene_node->attachObject(entity);
  }
  cloned_scene_node->setScale(scene_node->getScale());
  cloned_scene_node->setPosition(scene_node->getPosition());
  cloned_scene_node->setOrientation(scene_node->getOrientation());

  Ogre::SceneNode::ChildNodeIterator nodei = scene_node->getChildIterator();
  while (nodei.hasMoreElements())
  {
    Ogre::SceneNode* child_node = static_cast<Ogre::SceneNode*>(nodei.getNext());

    Ogre::SceneNode* cloned_child_scene_node = cloned_scene_node->createChildSceneNode();

    Ogre::SceneNode::ObjectIterator child_node_iter = child_node->getAttachedObjectIterator();
    while (child_node_iter.hasMoreElements())
    {
      Ogre::Entity* entity = static_cast<Ogre::Entity*>(child_node_iter.getNext())->clone(clone_link_name_generator.generate());

      if (isVisual)
        visual_trajectory_meshes_.push_back(entity);
      else
        collision_trajectory_meshes_.push_back(entity);

      cloned_child_scene_node->attachObject(entity);
    }
    cloned_child_scene_node->setScale(child_node->getScale());
    cloned_child_scene_node->setPosition(child_node->getPosition());
    cloned_child_scene_node->setOrientation(child_node->getOrientation());
  }

  return cloned_scene_node;
}

void EnvLink::setOctomapColor(double z_pos,
                                double min_z,
                                double max_z,
                                double color_factor,
                                rviz::PointCloud::Point* point)
{
  int i;
  float m, n, f;

  float s = 1.0f;
  float v = 1.0f;

  float h = static_cast<float>((1.0 - std::min(std::max((z_pos - min_z) / (max_z - min_z), 0.0), 1.0)) * color_factor);

  h -= static_cast<float>(floor(static_cast<double>(h)));
  h *= 6;
  i = static_cast<int>(floor(static_cast<double>(h)));
  f = h - static_cast<float>(i);
  if (!(i & 1))
    f = 1 - f;  // if i is even
  m = v * (1 - s);
  n = v * (1 - s * f);

  switch (i)
  {
    case 6:
    case 0:
      point->setColor(v, n, m);
      break;
    case 1:
      point->setColor(n, v, m);
      break;
    case 2:
      point->setColor(m, v, n);
      break;
    case 3:
      point->setColor(m, n, v);
      break;
    case 4:
      point->setColor(n, m, v);
      break;
    case 5:
      point->setColor(v, m, n);
      break;
    default:
      point->setColor(1.0f, 0.5f, 0.5f);
      break;
  }
}

void EnvLink::createCollision(const tesseract_scene_graph::Link& link)
{
  std::vector<tesseract_scene_graph::CollisionPtr>::const_iterator vi;
  for (vi = link.collision.begin(); vi != link.collision.end(); vi++)
  {
    tesseract_scene_graph::CollisionPtr collision = *vi;
    if (collision && collision->geometry)
    {
      createEntityForGeometryElement(link, *collision->geometry, collision->origin, "", false);
    }
  }

  collision_node_->setVisible(getEnabled());
}

void EnvLink::createVisual(const tesseract_scene_graph::Link& link)
{
  std::vector<tesseract_scene_graph::VisualPtr>::const_iterator vi;
  for (vi = link.visual.begin(); vi != link.visual.end(); vi++)
  {
    tesseract_scene_graph::VisualPtr visual = *vi;
    if (visual && visual->geometry)
    {
      createEntityForGeometryElement(link, *visual->geometry, visual->origin, visual->material_name, true);
    }
  }

  visual_node_->setVisible(getEnabled());
}

void EnvLink::createSelection()
{
  selection_handler_.reset(new EnvLinkSelectionHandler(this, context_));
  for (size_t i = 0; i < visual_meshes_.size(); i++)
  {
    selection_handler_->addTrackedObject(visual_meshes_[i]);
  }
  for (size_t i = 0; i < collision_meshes_.size(); i++)
  {
    selection_handler_->addTrackedObject(collision_meshes_[i]);
  }
  for (size_t i = 0; i < visual_octrees_.size(); i++)
  {
    selection_handler_->addTrackedObject(visual_octrees_[i]);
  }
  for (size_t i = 0; i < collision_octrees_.size(); i++)
  {
    selection_handler_->addTrackedObject(collision_octrees_[i]);
  }
}

void EnvLink::updateTrail()
{
  if (trail_property_->getValue().toBool())
  {
    if (!trail_)
    {
      if (visual_node_)
      {
        trail_ = scene_manager_->createRibbonTrail(trail_name_generator.generate());
        trail_->setMaxChainElements(100);
        trail_->setInitialWidth(0, 0.01f);
        trail_->setInitialColour(0, 0.0f, 0.5f, 0.5f);
        trail_->addNode(visual_node_);
        trail_->setTrailLength(2.0f);
        trail_->setVisible(getEnabled());
        env_->getOtherNode()->attachObject(trail_);
      }
      else
      {
        ROS_WARN("No visual node for link %s, cannot create a trail", name_.c_str());
      }
    }
  }
  else
  {
    if (trail_)
    {
      scene_manager_->destroyRibbonTrail(trail_);
      trail_ = nullptr;
    }
  }
}

void EnvLink::updateAxes()
{
  if (axes_property_->getValue().toBool())
  {
    if (!axes_)
    {
      axes_ = new rviz::Axes(scene_manager_, env_->getOtherNode(), 0.1f, 0.01f);
      axes_->getSceneNode()->setVisible(getEnabled());

      axes_->setPosition(position_property_->getVector());
      axes_->setOrientation(orientation_property_->getQuaternion());
    }
  }
  else
  {
    if (axes_)
    {
      delete axes_;
      axes_ = nullptr;
    }
  }
}

void EnvLink::setTransforms(const Ogre::Vector3& visual_position,
                              const Ogre::Quaternion& visual_orientation,
                              const Ogre::Vector3& collision_position,
                              const Ogre::Quaternion& collision_orientation)
{
  if (visual_node_)
  {
    visual_node_->setPosition(visual_position);
    visual_node_->setOrientation(visual_orientation);
  }

  if (collision_node_)
  {
    collision_node_->setPosition(collision_position);
    collision_node_->setOrientation(collision_orientation);
  }

  position_property_->setVector(visual_position);
  orientation_property_->setQuaternion(visual_orientation);

  if (axes_)
  {
    axes_->setPosition(visual_position);
    axes_->setOrientation(visual_orientation);
  }
}

void EnvLink::setTrajectory(const std::vector<Eigen::Isometry3d>& trajectory)
{
  clearTrajectory();

  bool enabled = getEnabled();

  int trajectory_size = trajectory.size();
  int current_size = visual_trajectory_waypoint_nodes_.size();
  if (trajectory_size > current_size)
  {
    for (int i = 0; i < trajectory_size; ++i)
    {
      Ogre::Vector3 position;
      Ogre::Quaternion orientation;
      toOgre(position, orientation, trajectory[i]);
      if (i < current_size)
      {
        if (visual_node_ != nullptr)
        {
          visual_trajectory_waypoint_nodes_[i]->setPosition(position);
          visual_trajectory_waypoint_nodes_[i]->setOrientation(orientation);
          visual_trajectory_waypoint_nodes_[i]->setVisible(enabled && env_->isVisible() && env_->isVisualVisible());
        }

        if (collision_node_ != nullptr)
        {
          collision_trajectory_waypoint_nodes_[i]->setPosition(position);
          collision_trajectory_waypoint_nodes_[i]->setOrientation(orientation);
          collision_trajectory_waypoint_nodes_[i]->setVisible(enabled && env_->isVisible() && env_->isCollisionVisible());
        }
      }
      else
      {
        if (visual_node_ != nullptr)
        {
          Ogre::SceneNode* new_visual_clone = clone(visual_node_, true);
          new_visual_clone->setPosition(position);
          new_visual_clone->setOrientation(orientation);
          new_visual_clone->setVisible(enabled && env_->isVisible() && env_->isVisualVisible());
          visual_trajectory_waypoint_nodes_.push_back(new_visual_clone);
        }

        if (collision_node_ != nullptr)
        {
          Ogre::SceneNode* new_collision_clone = clone(collision_node_, false);
          new_collision_clone->setPosition(position);
          new_collision_clone->setOrientation(orientation);
          new_collision_clone->setVisible(enabled && env_->isVisible() && env_->isCollisionVisible());
          collision_trajectory_waypoint_nodes_.push_back(new_collision_clone);
        }
      }
    }
  }
  else if(current_size >= trajectory_size)
  {
    for (int i = 0; i < current_size; ++i)
    {
      if (i < trajectory_size)
      {
        Ogre::Vector3 position;
        Ogre::Quaternion orientation;
        toOgre(position, orientation, trajectory[i]);

        if (visual_node_ != nullptr)
        {
          visual_trajectory_waypoint_nodes_[i]->setPosition(position);
          visual_trajectory_waypoint_nodes_[i]->setOrientation(orientation);
          visual_trajectory_waypoint_nodes_[i]->setVisible(enabled && env_->isVisible() && env_->isVisualVisible());
        }

        if (collision_node_ != nullptr)
        {
          collision_trajectory_waypoint_nodes_[i]->setPosition(position);
          collision_trajectory_waypoint_nodes_[i]->setOrientation(orientation);
          collision_trajectory_waypoint_nodes_[i]->setVisible(enabled && env_->isVisible() && env_->isCollisionVisible());
        }
      }
      else
      {
        if (visual_node_ != nullptr)
          visual_trajectory_waypoint_nodes_[i]->setVisible(false);

        if (collision_node_ != nullptr)
          collision_trajectory_waypoint_nodes_[i]->setVisible(false);
      }
    }
  }
}

void EnvLink::clearTrajectory()
{
  bool enabled = getEnabled();

  if (visual_node_)
  {
    visual_trajectory_node_->setVisible(false);
    visual_trajectory_node_->setVisible(enabled && env_->isVisible() && env_->isVisualVisible(), false);
  }

  if (collision_node_)
  {
    collision_trajectory_node_->setVisible(false);
    collision_trajectory_node_->setVisible(enabled && env_->isVisible() && env_->isCollisionVisible(), false);
  }
}

// This is usefule when wanting to simulate the trajectory
void EnvLink::showTrajectoryWaypointOnly(int waypoint)
{
  clearTrajectory();

  bool enabled = getEnabled();

  if (visual_node_ && (visual_trajectory_waypoint_nodes_.size() > waypoint))
    visual_trajectory_waypoint_nodes_[waypoint]->setVisible(enabled && env_->isVisible() && env_->isVisualVisible());

  if (collision_node_ && (collision_trajectory_waypoint_nodes_.size() > waypoint))
    collision_trajectory_waypoint_nodes_[waypoint]->setVisible(enabled && env_->isVisible() && env_->isCollisionVisible());

}

void EnvLink::setToErrorMaterial()
{
  for (size_t i = 0; i < visual_meshes_.size(); i++)
  {
    visual_meshes_[i]->setMaterialName("BaseWhiteNoLighting");
  }
  for (size_t i = 0; i < collision_meshes_.size(); i++)
  {
    collision_meshes_[i]->setMaterialName("BaseWhiteNoLighting");
  }

  // Currently not handling color for octree_objects_
}

void EnvLink::setToNormalMaterial()
{
  if (using_color_)
  {
    for (size_t i = 0; i < visual_meshes_.size(); i++)
    {
      visual_meshes_[i]->setMaterial(color_material_);
    }
    for (size_t i = 0; i < collision_meshes_.size(); i++)
    {
      collision_meshes_[i]->setMaterial(color_material_);
    }

    // Currently not handling color for octree_objects_
  }
  else
  {
    M_SubEntityToMaterial::iterator it = materials_.begin();
    M_SubEntityToMaterial::iterator end = materials_.end();
    for (; it != end; ++it)
    {
      it->first->setMaterial(it->second);
    }
  }
}

void EnvLink::setColor(float red, float green, float blue)
{
  Ogre::ColourValue color = color_material_->getTechnique(0)->getPass(0)->getDiffuse();
  color.r = red;
  color.g = green;
  color.b = blue;
  color_material_->getTechnique(0)->setAmbient(0.5 * color);
  color_material_->getTechnique(0)->setDiffuse(color);

  using_color_ = true;
  setToNormalMaterial();
}

void EnvLink::unsetColor()
{
  using_color_ = false;
  setToNormalMaterial();
}

bool EnvLink::setSelectable(bool selectable)
{
  bool old = is_selectable_;
  is_selectable_ = selectable;
  return old;
}

bool EnvLink::getSelectable() { return is_selectable_; }
void EnvLink::hideSubProperties(bool hide)
{
  position_property_->setHidden(hide);
  orientation_property_->setHidden(hide);
  trail_property_->setHidden(hide);
  axes_property_->setHidden(hide);
  alpha_property_->setHidden(hide);
  collision_enabled_property_->setHidden(hide);
  allowed_collision_matrix_property_->setHidden(hide);
}

Ogre::Vector3 EnvLink::getPosition() { return position_property_->getVector(); }
Ogre::Quaternion EnvLink::getOrientation() { return orientation_property_->getQuaternion(); }
void EnvLink::setParentProperty(rviz::Property* new_parent)
{
  rviz::Property* old_parent = link_property_->getParent();
  if (old_parent)
    old_parent->takeChild(link_property_);

  if (new_parent)
    new_parent->addChild(link_property_);
}

void EnvLink::setCollisionEnabled(bool enabled)
{
  if(enabled)
    collision_enabled_property_->setString("enabled");
  else
    collision_enabled_property_->setString("disabled");
}

void EnvLink::addAllowedCollision(const std::string& link_name, const std::string& reason)
{
  acm_[link_name] = new rviz::StringProperty(QString::fromStdString(link_name), QString::fromStdString(reason), "Entry", allowed_collision_matrix_property_);
}
void EnvLink::removeAllowedCollision(const std::string& link_name)
{
  auto it = acm_.find(link_name);
  if (it == acm_.end())
    return;

  allowed_collision_matrix_property_->takeChild(it->second);
  delete it->second;
  acm_.erase(link_name);

}
void EnvLink::clearAllowedCollisions()
{
  allowed_collision_matrix_property_->removeChildren();
}

// if use_detail:
//    - all sub properties become children of details_ property.
//    - details_ property becomes a child of link_property_
// else (!use_detail)
//    - all sub properties become children of link_property_.
//    details_ property does not have a parent.
void EnvLink::useDetailProperty(bool use_detail)
{
  rviz::Property* old_parent = details_->getParent();
  if (old_parent)
    old_parent->takeChild(details_);

  if (use_detail)
  {
    while (link_property_->numChildren() > 0)
    {
      rviz::Property* child = link_property_->childAt(0);
      link_property_->takeChild(child);
      details_->addChild(child);
    }

    link_property_->addChild(details_);
  }
  else
  {
    while (details_->numChildren() > 0)
    {
      rviz::Property* child = details_->childAt(0);
      details_->takeChild(child);
      link_property_->addChild(child);
    }
  }
}

void EnvLink::expandDetails(bool expand)
{
  rviz::Property* parent = details_->getParent() ? details_ : link_property_;
  if (expand)
  {
    parent->expand();
  }
  else
  {
    parent->collapse();
  }
}

}  // namespace rviz