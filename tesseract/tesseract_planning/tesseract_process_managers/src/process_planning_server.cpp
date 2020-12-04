/**
 * @file process_planning_server.cpp
 * @brief A process planning server with a default set of process planners
 *
 * @author Levi Armstrong
 * @date August 18, 2020
 * @version TODO
 * @bug No known bugs
 *
 * @copyright Copyright (c) 2020, Southwest Research Institute
 *
 * @par License
 * Software License Agreement (Apache License)
 * @par
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * @par
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <tesseract_common/macros.h>
TESSERACT_COMMON_IGNORE_WARNINGS_PUSH
#include <console_bridge/console.h>
TESSERACT_COMMON_IGNORE_WARNINGS_POP

#include <tesseract_process_managers/process_planning_server.h>
#include <tesseract_process_managers/debug_observer.h>
#include <tesseract_process_managers/taskflows/cartesian_taskflow.h>
#include <tesseract_process_managers/taskflows/descartes_taskflow.h>
#include <tesseract_process_managers/taskflows/freespace_taskflow.h>
#include <tesseract_process_managers/taskflows/ompl_taskflow.h>
#include <tesseract_process_managers/taskflows/trajopt_taskflow.h>
#include <tesseract_process_managers/taskflow_generators/graph_taskflow.h>
#include <tesseract_process_managers/taskflow_generators/raster_taskflow.h>
#include <tesseract_process_managers/taskflow_generators/raster_global_taskflow.h>
#include <tesseract_process_managers/taskflow_generators/raster_only_taskflow.h>
#include <tesseract_process_managers/taskflow_generators/raster_only_global_taskflow.h>
#include <tesseract_process_managers/taskflow_generators/raster_dt_taskflow.h>
#include <tesseract_process_managers/taskflow_generators/raster_waad_taskflow.h>
#include <tesseract_process_managers/taskflow_generators/raster_waad_dt_taskflow.h>

#include <tesseract_motion_planners/descartes/profile/descartes_profile.h>
#include <tesseract_motion_planners/trajopt/profile/trajopt_profile.h>
#include <tesseract_motion_planners/ompl/profile/ompl_profile.h>
#include <tesseract_motion_planners/descartes/profile/descartes_profile.h>
#include <tesseract_motion_planners/simple/profile/simple_planner_profile.h>

#include <tesseract_command_language/utils/utils.h>

namespace tesseract_planning
{
TaskflowGenerator::UPtr createTrajOptGenerator(bool enable_simple_planner, ProfileDictionary::ConstPtr profiles)
{
  TrajOptTaskflowParams params;
  params.enable_simple_planner = enable_simple_planner;
  params.profiles = profiles;
  return createTrajOptTaskflow(params);
}

TaskflowGenerator::UPtr createOMPLGenerator(bool enable_simple_planner, ProfileDictionary::ConstPtr profiles)
{
  OMPLTaskflowParams params;
  params.enable_simple_planner = enable_simple_planner;
  params.profiles = profiles;
  return createOMPLTaskflow(params);
}

TaskflowGenerator::UPtr createDescartesGenerator(bool enable_simple_planner, ProfileDictionary::ConstPtr profiles)
{
  DescartesTaskflowParams params;
  params.enable_simple_planner = enable_simple_planner;
  params.profiles = profiles;
  return createDescartesTaskflow(params);
}

TaskflowGenerator::UPtr createCartesianGenerator(bool enable_simple_planner, ProfileDictionary::ConstPtr profiles)
{
  CartesianTaskflowParams params;
  params.enable_simple_planner = enable_simple_planner;
  params.profiles = profiles;
  return createCartesianTaskflow(params);
}

TaskflowGenerator::UPtr createFreespaceGenerator(bool enable_simple_planner, ProfileDictionary::ConstPtr profiles)
{
  FreespaceTaskflowParams params;
  params.enable_simple_planner = enable_simple_planner;
  params.profiles = profiles;
  return createFreespaceTaskflow(params);
}

TaskflowGenerator::UPtr createRasterGenerator(bool enable_simple_planner, ProfileDictionary::ConstPtr profiles)
{
  // Create Freespace and Transition Taskflows
  FreespaceTaskflowParams fparams;
  fparams.enable_simple_planner = enable_simple_planner;
  fparams.profiles = profiles;
  GraphTaskflow::UPtr freespace_task = createFreespaceTaskflow(fparams);
  GraphTaskflow::UPtr transition_task = createFreespaceTaskflow(fparams);

  // Create Raster Taskflow
  CartesianTaskflowParams cparams;
  cparams.enable_simple_planner = enable_simple_planner;
  cparams.profiles = profiles;
  GraphTaskflow::UPtr raster_task = createCartesianTaskflow(cparams);

  return std::make_unique<RasterTaskflow>(
      std::move(freespace_task), std::move(transition_task), std::move(raster_task));
}

TaskflowGenerator::UPtr createRasterOnlyGenerator(bool enable_simple_planner, ProfileDictionary::ConstPtr profiles)
{
  // Create Freespace and Transition Taskflows
  FreespaceTaskflowParams tparams;
  tparams.enable_simple_planner = enable_simple_planner;
  tparams.profiles = profiles;
  GraphTaskflow::UPtr transition_task = createFreespaceTaskflow(tparams);

  // Create Raster Taskflow
  CartesianTaskflowParams cparams;
  cparams.enable_simple_planner = enable_simple_planner;
  cparams.profiles = profiles;
  GraphTaskflow::UPtr raster_task = createCartesianTaskflow(cparams);

  return std::make_unique<RasterOnlyTaskflow>(std::move(transition_task), std::move(raster_task));
}

TaskflowGenerator::UPtr createRasterGlobalGenerator(bool enable_simple_planner, ProfileDictionary::ConstPtr profiles)
{
  DescartesTaskflowParams global_params;
  global_params.enable_simple_planner = enable_simple_planner;
  global_params.enable_post_contact_discrete_check = false;
  global_params.enable_post_contact_continuous_check = false;
  global_params.enable_time_parameterization = false;
  global_params.profiles = profiles;
  GraphTaskflow::UPtr global_task = createDescartesTaskflow(global_params);

  FreespaceTaskflowParams freespace_params;
  freespace_params.type = FreespaceTaskflowType::TRAJOPT_FIRST;
  freespace_params.enable_simple_planner = false;
  freespace_params.profiles = profiles;
  GraphTaskflow::UPtr freespace_task = createFreespaceTaskflow(freespace_params);
  GraphTaskflow::UPtr transition_task = createFreespaceTaskflow(freespace_params);

  TrajOptTaskflowParams raster_params;
  raster_params.enable_simple_planner = false;
  raster_params.profiles = profiles;
  GraphTaskflow::UPtr raster_task = createTrajOptTaskflow(raster_params);

  return std::make_unique<RasterGlobalTaskflow>(
      std::move(global_task), std::move(freespace_task), std::move(transition_task), std::move(raster_task));
}

TaskflowGenerator::UPtr createRasterDTGenerator(bool enable_simple_planner, ProfileDictionary::ConstPtr profiles)
{
  // Create Freespace and Transition Taskflows
  FreespaceTaskflowParams fparams;
  fparams.enable_simple_planner = enable_simple_planner;
  fparams.profiles = profiles;
  GraphTaskflow::UPtr freespace_task = createFreespaceTaskflow(fparams);
  GraphTaskflow::UPtr transition_task = createFreespaceTaskflow(fparams);

  // Create Raster Taskflow
  CartesianTaskflowParams cparams;
  cparams.enable_simple_planner = enable_simple_planner;
  cparams.profiles = profiles;
  GraphTaskflow::UPtr raster_task = createCartesianTaskflow(cparams);

  return std::make_unique<RasterDTTaskflow>(
      std::move(freespace_task), std::move(transition_task), std::move(raster_task));
}

TaskflowGenerator::UPtr createRasterWAADGenerator(bool enable_simple_planner, ProfileDictionary::ConstPtr profiles)
{
  // Create Freespace and Transition Taskflows
  FreespaceTaskflowParams fparams;
  fparams.enable_simple_planner = enable_simple_planner;
  fparams.profiles = profiles;
  GraphTaskflow::UPtr freespace_task = createFreespaceTaskflow(fparams);
  GraphTaskflow::UPtr transition_task = createFreespaceTaskflow(fparams);

  // Create Raster Taskflow
  CartesianTaskflowParams cparams;
  cparams.enable_simple_planner = enable_simple_planner;
  cparams.profiles = profiles;
  GraphTaskflow::UPtr raster_task = createCartesianTaskflow(cparams);

  return std::make_unique<RasterWAADTaskflow>(
      std::move(freespace_task), std::move(transition_task), std::move(raster_task));
}

TaskflowGenerator::UPtr createRasterWAADDTGenerator(bool enable_simple_planner, ProfileDictionary::ConstPtr profiles)
{
  // Create Freespace and Transition Taskflows
  FreespaceTaskflowParams fparams;
  fparams.enable_simple_planner = enable_simple_planner;
  fparams.profiles = profiles;
  GraphTaskflow::UPtr freespace_task = createFreespaceTaskflow(fparams);
  GraphTaskflow::UPtr transition_task = createFreespaceTaskflow(fparams);

  // Create Raster Taskflow
  CartesianTaskflowParams cparams;
  cparams.enable_simple_planner = enable_simple_planner;
  cparams.profiles = profiles;
  GraphTaskflow::UPtr raster_task = createCartesianTaskflow(cparams);

  return std::make_unique<RasterWAADDTTaskflow>(
      std::move(freespace_task), std::move(transition_task), std::move(raster_task));
}

TaskflowGenerator::UPtr createRasterOnlyGlobalGenerator(bool enable_simple_planner,
                                                        ProfileDictionary::ConstPtr profiles)
{
  DescartesTaskflowParams global_params;
  global_params.enable_simple_planner = enable_simple_planner;
  global_params.enable_post_contact_discrete_check = false;
  global_params.enable_post_contact_continuous_check = false;
  global_params.enable_time_parameterization = false;
  global_params.profiles = profiles;
  GraphTaskflow::UPtr global_task = createDescartesTaskflow(global_params);

  FreespaceTaskflowParams transition_params;
  transition_params.type = FreespaceTaskflowType::TRAJOPT_FIRST;
  transition_params.enable_simple_planner = false;
  transition_params.profiles = profiles;
  GraphTaskflow::UPtr transition_task = createFreespaceTaskflow(transition_params);

  TrajOptTaskflowParams raster_params;
  raster_params.enable_simple_planner = false;
  raster_params.profiles = profiles;
  GraphTaskflow::UPtr raster_task = createTrajOptTaskflow(raster_params);

  return std::make_unique<RasterOnlyGlobalTaskflow>(
      std::move(global_task), std::move(transition_task), std::move(raster_task));
}

TaskflowGenerator::UPtr createRasterCTGenerator(bool enable_simple_planner, ProfileDictionary::ConstPtr profiles)
{
  // Create Freespace and Transition Taskflows
  FreespaceTaskflowParams freespace_params;
  freespace_params.enable_simple_planner = enable_simple_planner;
  freespace_params.profiles = profiles;
  GraphTaskflow::UPtr freespace_task = createFreespaceTaskflow(freespace_params);

  // Create Raster Taskflow
  CartesianTaskflowParams cartesian_params;
  cartesian_params.enable_simple_planner = enable_simple_planner;
  cartesian_params.profiles = profiles;
  GraphTaskflow::UPtr raster_task = createCartesianTaskflow(cartesian_params);
  GraphTaskflow::UPtr transition_task = createCartesianTaskflow(cartesian_params);

  return std::make_unique<RasterTaskflow>(
      std::move(freespace_task), std::move(transition_task), std::move(raster_task));
}

TaskflowGenerator::UPtr createRasterOnlyCTGenerator(bool enable_simple_planner, ProfileDictionary::ConstPtr profiles)
{
  // Create Transition and Raster Taskflow
  CartesianTaskflowParams cartesian_params;
  cartesian_params.enable_simple_planner = enable_simple_planner;
  cartesian_params.profiles = profiles;
  GraphTaskflow::UPtr raster_task = createCartesianTaskflow(cartesian_params);
  GraphTaskflow::UPtr transition_task = createCartesianTaskflow(cartesian_params);

  return std::make_unique<RasterOnlyTaskflow>(std::move(transition_task), std::move(raster_task));
}

TaskflowGenerator::UPtr createRasterCTDTGenerator(bool enable_simple_planner, ProfileDictionary::ConstPtr profiles)
{
  // Create Freespace and Transition Taskflows
  FreespaceTaskflowParams freespace_params;
  freespace_params.enable_simple_planner = enable_simple_planner;
  freespace_params.profiles = profiles;
  GraphTaskflow::UPtr freespace_task = createFreespaceTaskflow(freespace_params);

  // Create Raster Taskflow
  CartesianTaskflowParams cartesian_params;
  cartesian_params.enable_simple_planner = enable_simple_planner;
  cartesian_params.profiles = profiles;
  GraphTaskflow::UPtr raster_task = createCartesianTaskflow(cartesian_params);
  GraphTaskflow::UPtr transition_task = createCartesianTaskflow(cartesian_params);

  return std::make_unique<RasterDTTaskflow>(
      std::move(freespace_task), std::move(transition_task), std::move(raster_task));
}

TaskflowGenerator::UPtr createRasterCTWAADGenerator(bool enable_simple_planner, ProfileDictionary::ConstPtr profiles)
{
  // Create Freespace and Transition Taskflows
  FreespaceTaskflowParams freespace_params;
  freespace_params.enable_simple_planner = enable_simple_planner;
  freespace_params.profiles = profiles;
  GraphTaskflow::UPtr freespace_task = createFreespaceTaskflow(freespace_params);

  // Create Raster Taskflow
  CartesianTaskflowParams cartesian_params;
  cartesian_params.enable_simple_planner = enable_simple_planner;
  cartesian_params.profiles = profiles;
  GraphTaskflow::UPtr raster_task = createCartesianTaskflow(cartesian_params);
  GraphTaskflow::UPtr transition_task = createCartesianTaskflow(cartesian_params);

  return std::make_unique<RasterWAADTaskflow>(
      std::move(freespace_task), std::move(transition_task), std::move(raster_task));
}

TaskflowGenerator::UPtr createRasterCTWAADDTGenerator(bool enable_simple_planner, ProfileDictionary::ConstPtr profiles)
{
  // Create Freespace and Transition Taskflows
  FreespaceTaskflowParams freespace_params;
  freespace_params.enable_simple_planner = enable_simple_planner;
  freespace_params.profiles = profiles;
  GraphTaskflow::UPtr freespace_task = createFreespaceTaskflow(freespace_params);

  // Create Raster Taskflow
  CartesianTaskflowParams cartesian_params;
  cartesian_params.enable_simple_planner = enable_simple_planner;
  cartesian_params.profiles = profiles;
  GraphTaskflow::UPtr raster_task = createCartesianTaskflow(cartesian_params);
  GraphTaskflow::UPtr transition_task = createCartesianTaskflow(cartesian_params);

  return std::make_unique<RasterWAADDTTaskflow>(
      std::move(freespace_task), std::move(transition_task), std::move(raster_task));
}

TaskflowGenerator::UPtr createRasterGlobalCTGenerator(bool enable_simple_planner, ProfileDictionary::ConstPtr profiles)
{
  DescartesTaskflowParams global_params;
  global_params.enable_simple_planner = enable_simple_planner;
  global_params.enable_post_contact_discrete_check = false;
  global_params.enable_post_contact_continuous_check = false;
  global_params.enable_time_parameterization = false;
  global_params.profiles = profiles;
  GraphTaskflow::UPtr global_task = createDescartesTaskflow(global_params);

  FreespaceTaskflowParams freespace_params;
  freespace_params.type = FreespaceTaskflowType::TRAJOPT_FIRST;
  freespace_params.enable_simple_planner = false;
  freespace_params.profiles = profiles;
  GraphTaskflow::UPtr freespace_task = createFreespaceTaskflow(freespace_params);

  TrajOptTaskflowParams raster_params;
  raster_params.enable_simple_planner = false;
  raster_params.profiles = profiles;
  GraphTaskflow::UPtr raster_task = createTrajOptTaskflow(raster_params);
  GraphTaskflow::UPtr transition_task = createTrajOptTaskflow(raster_params);

  return std::make_unique<RasterGlobalTaskflow>(
      std::move(global_task), std::move(freespace_task), std::move(transition_task), std::move(raster_task));
}

TaskflowGenerator::UPtr createRasterOnlyGlobalCTGenerator(bool enable_simple_planner,
                                                          ProfileDictionary::ConstPtr profiles)
{
  DescartesTaskflowParams global_params;
  global_params.enable_simple_planner = enable_simple_planner;
  global_params.enable_post_contact_discrete_check = false;
  global_params.enable_post_contact_continuous_check = false;
  global_params.enable_time_parameterization = false;
  global_params.profiles = profiles;
  GraphTaskflow::UPtr global_task = createDescartesTaskflow(global_params);

  TrajOptTaskflowParams raster_params;
  raster_params.enable_simple_planner = false;
  raster_params.profiles = profiles;
  GraphTaskflow::UPtr raster_task = createTrajOptTaskflow(raster_params);
  GraphTaskflow::UPtr transition_task = createTrajOptTaskflow(raster_params);

  return std::make_unique<RasterOnlyGlobalTaskflow>(
      std::move(global_task), std::move(transition_task), std::move(raster_task));
}

ProcessPlanningServer::ProcessPlanningServer(EnvironmentCache::Ptr cache, size_t n)
  : cache_(std::move(cache)), executor_(std::make_shared<tf::Executor>(n))
{
  /** @todo Need to figure out if these can associated with an individual run versus global */
  executor_->make_observer<DebugObserver>("ProcessPlanningObserver");
}

void ProcessPlanningServer::registerProcessPlanner(const std::string& name, ProcessPlannerGeneratorFn generator)
{
  if (process_planners_.find(name) != process_planners_.end())
    CONSOLE_BRIDGE_logDebug("Process planner %s already exist so replacing with new generator.", name.c_str());

  process_planners_[name] = generator;
}

void ProcessPlanningServer::loadDefaultProcessPlanners()
{
  registerProcessPlanner(process_planner_names::TRAJOPT_PLANNER_NAME, &createTrajOptGenerator);
  registerProcessPlanner(process_planner_names::OMPL_PLANNER_NAME, &createOMPLGenerator);
  registerProcessPlanner(process_planner_names::DESCARTES_PLANNER_NAME, &createDescartesGenerator);
  registerProcessPlanner(process_planner_names::CARTESIAN_PLANNER_NAME, &createCartesianGenerator);
  registerProcessPlanner(process_planner_names::FREESPACE_PLANNER_NAME, &createFreespaceGenerator);
  registerProcessPlanner(process_planner_names::RASTER_FT_PLANNER_NAME, &createRasterGenerator);
  registerProcessPlanner(process_planner_names::RASTER_O_FT_PLANNER_NAME, &createRasterOnlyGenerator);
  registerProcessPlanner(process_planner_names::RASTER_G_FT_PLANNER_NAME, &createRasterGlobalGenerator);
  registerProcessPlanner(process_planner_names::RASTER_FT_DT_PLANNER_NAME, &createRasterDTGenerator);
  registerProcessPlanner(process_planner_names::RASTER_FT_WAAD_PLANNER_NAME, &createRasterWAADGenerator);
  registerProcessPlanner(process_planner_names::RASTER_FT_WAAD_DT_PLANNER_NAME, &createRasterWAADDTGenerator);
  registerProcessPlanner(process_planner_names::RASTER_O_G_FT_PLANNER_NAME, &createRasterOnlyGlobalGenerator);
  registerProcessPlanner(process_planner_names::RASTER_CT_PLANNER_NAME, &createRasterCTGenerator);
  registerProcessPlanner(process_planner_names::RASTER_O_CT_PLANNER_NAME, &createRasterOnlyCTGenerator);
  registerProcessPlanner(process_planner_names::RASTER_CT_DT_PLANNER_NAME, &createRasterCTDTGenerator);
  registerProcessPlanner(process_planner_names::RASTER_CT_WAAD_PLANNER_NAME, &createRasterCTWAADGenerator);
  registerProcessPlanner(process_planner_names::RASTER_CT_WAAD_DT_PLANNER_NAME, &createRasterCTWAADDTGenerator);
  registerProcessPlanner(process_planner_names::RASTER_G_CT_PLANNER_NAME, &createRasterGlobalCTGenerator);
  registerProcessPlanner(process_planner_names::RASTER_O_G_CT_PLANNER_NAME, &createRasterOnlyGlobalCTGenerator);
}

bool ProcessPlanningServer::hasProcessPlanner(const std::string& name) const
{
  return (process_planners_.find(name) != process_planners_.end());
}

std::vector<std::string> ProcessPlanningServer::getAvailableProcessPlanners() const
{
  std::vector<std::string> planners;
  planners.reserve(process_planners_.size());
  for (const auto& planner : process_planners_)
    planners.push_back(planner.first);

  return planners;
}

ProcessPlanningFuture ProcessPlanningServer::run(const ProcessPlanningRequest& request)
{
  CONSOLE_BRIDGE_logInform("Tesseract Planning Server Recieved Request!");
  ProcessPlanningFuture response;
  response.plan_profile_remapping = std::make_unique<const PlannerProfileRemapping>(request.plan_profile_remapping);
  response.composite_profile_remapping =
      std::make_unique<const PlannerProfileRemapping>(request.composite_profile_remapping);

  response.input = std::make_unique<Instruction>(request.instructions);
  const auto* composite_program = response.input->cast_const<CompositeInstruction>();
  ManipulatorInfo mi = composite_program->getManipulatorInfo();
  response.global_manip_info = std::make_unique<const ManipulatorInfo>(mi);

  bool enable_simple_planner = isNullInstruction(request.seed);
  if (!enable_simple_planner)
    response.results = std::make_unique<Instruction>(request.seed);
  else
    response.results = std::make_unique<Instruction>(generateSkeletonSeed(*composite_program));

  auto it = process_planners_.find(request.name);
  if (it != process_planners_.end())
  {
    response.taskflow_generator = it->second(enable_simple_planner, profiles_);
  }
  else
  {
    CONSOLE_BRIDGE_logError("Requested motion planner is not supported!");
    return response;
  }

  assert(response.taskflow_generator != nullptr);
  tesseract::Tesseract::Ptr tc = cache_->getCachedEnvironment();

  // Set the env state if provided
  if (request.env_state != nullptr)
    tc->getEnvironment()->setState(request.env_state->joints);

  if (!request.commands.empty() && !tc->getEnvironment()->applyCommands(request.commands))
  {
    CONSOLE_BRIDGE_logInform("Tesseract Planning Server Finished Request!");
    return response;
  }

  //  response.process_manager->enableDebug(request.debug);
  //  response.process_manager->enableProfile(request.profile);
  bool* s = response.success.get();
  auto done_cb = [s]() {
    *s &= true;
    CONSOLE_BRIDGE_logError("Done Callback");
  };
  auto error_cb = [s]() {
    *s = false;
    CONSOLE_BRIDGE_logError("Error Callback");
  };

  tf::Taskflow& taskflow =
      response.taskflow_generator->generateTaskflow(ProcessInput(tc,
                                                                 response.input.get(),
                                                                 *(response.global_manip_info),
                                                                 *(response.plan_profile_remapping),
                                                                 *(response.composite_profile_remapping),
                                                                 response.results.get(),
                                                                 request.debug),
                                                    done_cb,
                                                    error_cb);

  // Dump taskflow graph before running
  if (console_bridge::getLogLevel() >= console_bridge::LogLevel::CONSOLE_BRIDGE_LOG_INFO)
  {
    std::ofstream out_data;
    out_data.open(tesseract_common::getTempPath() + request.name + "-" + tesseract_common::getTimestampString() +
                  ".dot");
    taskflow.dump(out_data);
    out_data.close();
  }

  response.process_future = executor_->run(taskflow);
  return response;
}

std::future<void> ProcessPlanningServer::run(tf::Taskflow& taskflow) { return executor_->run(taskflow); }

void ProcessPlanningServer::waitForAll() { executor_->wait_for_all(); }

ProfileDictionary::Ptr ProcessPlanningServer::getProfiles() { return profiles_; }

ProfileDictionary::ConstPtr ProcessPlanningServer::getProfiles() const { return profiles_; }

}  // namespace tesseract_planning