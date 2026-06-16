import client from './client'

export default {
  getMaterials() {
    return client.get('/api/v1/materials/')
  },

  getRecipes() {
    return client.get('/api/v1/recipes/')
  },

  getRecipe(id) {
    return client.get(`/api/v1/recipes/${id}/`)
  },

  getRecipeSteps(id) {
    return client.get(`/api/v1/recipes/${id}/steps/`)
  }
}
