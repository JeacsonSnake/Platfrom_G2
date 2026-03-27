class LegacyApiDeprecationMiddleware:
    """
    Mark legacy /api/* routes (except /api/v1/*) as deprecated and provide
    a migration hint for clients.
    """

    def __init__(self, get_response):
        self.get_response = get_response

    def __call__(self, request):
        response = self.get_response(request)

        path = request.path
        if path.startswith('/api/') and not path.startswith('/api/v1/'):
            response['X-API-Deprecated'] = 'true'
            response['X-API-Replacement-Prefix'] = '/api/v1/'
            response['Warning'] = '299 - "Legacy API path is deprecated; migrate to /api/v1/."'

        return response
