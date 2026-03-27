from rest_framework import status
from rest_framework.test import APITestCase

from .models import (
    ExperimentProcess,
    MaterialType,
    MaterialRecipe,
    RecipeStep,
    BatchJob,
    BatchStepExecution,
    CommandOutbox,
)


class ExperimentProcessApiTests(APITestCase):
    def test_experiment_process_crud(self):
        create_payload = {
            "experiment_id": "EXP_1001",
            "experiment_name": "ZnO Trial",
            "dmac_dosage_ml": "20.0",
            "water_dosage_ml": "5.0",
            "solvent_ph": "7.2",
            "reaction_temperature_c": "85.50",
            "stirring_speed_rpm": 600,
            "stirring_duration_min": 30,
        }
        create_resp = self.client.post("/api/v1/experiments/", create_payload, format="json")
        self.assertEqual(create_resp.status_code, status.HTTP_201_CREATED)
        self.assertEqual(create_resp.data["experiment_id"], "EXP_1001")

        list_resp = self.client.get("/api/v1/experiments/")
        self.assertEqual(list_resp.status_code, status.HTTP_200_OK)
        self.assertEqual(len(list_resp.data), 1)

        detail_resp = self.client.get("/api/v1/experiments/EXP_1001/")
        self.assertEqual(detail_resp.status_code, status.HTTP_200_OK)
        self.assertEqual(detail_resp.data["experiment_name"], "ZnO Trial")

        patch_resp = self.client.patch(
            "/api/v1/experiments/EXP_1001/",
            {"stirring_speed_rpm": 700},
            format="json",
        )
        self.assertEqual(patch_resp.status_code, status.HTTP_200_OK)
        self.assertEqual(patch_resp.data["stirring_speed_rpm"], 700)

        delete_resp = self.client.delete("/api/v1/experiments/EXP_1001/")
        self.assertEqual(delete_resp.status_code, status.HTTP_204_NO_CONTENT)
        self.assertFalse(ExperimentProcess.objects.filter(experiment_id="EXP_1001").exists())


class RecipeAndJobApiTests(APITestCase):
    def setUp(self):
        self.material = MaterialType.objects.create(name="ZnO", description="Target material")
        self.recipe = MaterialRecipe.objects.create(
            material_type=self.material,
            version=1,
            dmac_dosage_ml="20.0",
            water_dosage_ml="5.0",
            solvent_ph="7.2",
            reaction_temperature_c="85.50",
            stirring_speed_rpm=600,
            stirring_duration_min=30,
        )
        self.step1 = RecipeStep.objects.create(
            recipe=self.recipe,
            step_no=1,
            step_type="MOVE_ARM",
            name="Move to pickup",
            parameters={"topic": "robot/arm/actions", "pose": "pickup"},
            expected_duration_sec=3,
        )
        self.step2 = RecipeStep.objects.create(
            recipe=self.recipe,
            step_no=2,
            step_type="STIR",
            name="Start stir",
            parameters={"topic": "robot/turntable/actions", "rpm_key": "stirring_speed_rpm"},
            expected_duration_sec=10,
        )

    def test_material_list_endpoint(self):
        resp = self.client.get("/api/v1/materials/")
        self.assertEqual(resp.status_code, status.HTTP_200_OK)
        self.assertEqual(len(resp.data), 1)
        self.assertEqual(resp.data[0]["name"], "ZnO")

    def test_recipe_list_and_detail_endpoint(self):
        list_resp = self.client.get("/api/v1/recipes/")
        self.assertEqual(list_resp.status_code, status.HTTP_200_OK)
        self.assertEqual(len(list_resp.data), 1)

        detail_resp = self.client.get(f"/api/v1/recipes/{self.recipe.id}/")
        self.assertEqual(detail_resp.status_code, status.HTTP_200_OK)
        self.assertEqual(detail_resp.data["material_type"], self.material.id)

    def test_recipe_step_list_endpoint(self):
        resp = self.client.get(f"/api/v1/recipes/{self.recipe.id}/steps/")
        self.assertEqual(resp.status_code, status.HTTP_200_OK)
        self.assertEqual(len(resp.data), 2)
        self.assertEqual(resp.data[0]["step_no"], 1)
        self.assertEqual(resp.data[1]["step_no"], 2)

    def test_job_create_success_creates_step_executions(self):
        resp = self.client.post(
            "/api/v1/jobs/",
            {
                "recipe_id": self.recipe.id,
                "operator": "operator_a",
                "overrides": {"reaction_temperature_c": 90.0},
            },
            format="json",
        )
        self.assertEqual(resp.status_code, status.HTTP_201_CREATED)
        job_id = resp.data["id"]

        job = BatchJob.objects.get(id=job_id)
        self.assertEqual(job.status, "PENDING")
        self.assertEqual(job.operator, "operator_a")
        self.assertEqual(BatchStepExecution.objects.filter(job=job).count(), 2)

    def test_job_create_rejects_invalid_overrides(self):
        resp = self.client.post(
            "/api/v1/jobs/",
            {
                "recipe_id": self.recipe.id,
                "overrides": ["not", "an", "object"],
            },
            format="json",
        )
        self.assertEqual(resp.status_code, status.HTTP_400_BAD_REQUEST)
        self.assertIn("overrides", resp.data)

    def test_job_create_rejects_recipe_without_steps(self):
        empty_recipe = MaterialRecipe.objects.create(
            material_type=self.material,
            version=2,
            dmac_dosage_ml="10.0",
        )
        resp = self.client.post(
            "/api/v1/jobs/",
            {"recipe_id": empty_recipe.id},
            format="json",
        )
        self.assertEqual(resp.status_code, status.HTTP_400_BAD_REQUEST)
        self.assertIn("detail", resp.data)

    def test_job_start_queues_all_pending_steps_and_outbox(self):
        create_resp = self.client.post("/api/v1/jobs/", {"recipe_id": self.recipe.id}, format="json")
        job_id = create_resp.data["id"]

        start_resp = self.client.post(f"/api/v1/jobs/{job_id}/start/", {}, format="json")
        self.assertEqual(start_resp.status_code, status.HTTP_200_OK)
        self.assertEqual(start_resp.data["status"], "RUNNING")

        queued_count = BatchStepExecution.objects.filter(job_id=job_id, status="QUEUED").count()
        self.assertEqual(queued_count, 2)
        self.assertEqual(CommandOutbox.objects.filter(job_id=job_id, status="QUEUED").count(), 2)

    def test_job_status_returns_counts_and_next_step(self):
        create_resp = self.client.post("/api/v1/jobs/", {"recipe_id": self.recipe.id}, format="json")
        job_id = create_resp.data["id"]
        self.client.post(f"/api/v1/jobs/{job_id}/start/", {}, format="json")

        status_resp = self.client.get(f"/api/v1/jobs/{job_id}/status/")
        self.assertEqual(status_resp.status_code, status.HTTP_200_OK)
        self.assertEqual(status_resp.data["job"]["id"], job_id)
        self.assertEqual(status_resp.data["step_status_counts"]["queued"], 2)
        self.assertIsNotNone(status_resp.data["next_step"])


class LegacyCompatibilityTests(APITestCase):
    def test_legacy_api_has_deprecation_headers(self):
        resp = self.client.get("/api/tasks/")
        self.assertEqual(resp.status_code, status.HTTP_200_OK)
        self.assertEqual(resp["X-API-Deprecated"], "true")
        self.assertEqual(resp["X-API-Replacement-Prefix"], "/api/v1/")
