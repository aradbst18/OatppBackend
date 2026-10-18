# Developer Component Guides

Step-by-step guides and conformance checklists for extending the OatppBackend application with new endpoints, controllers, DTOs, business services, and database repositories.

---

## Guides Directory

| Guide | Scope & Focus |
| :--- | :--- |
| **[`adding-api.md`](adding-api.md)** | End-to-end workflow for adding a new REST endpoint across all architecture layers with conformance checklist |
| **[`adding-controller.md`](adding-controller.md)** | Implementing Oat++ `ApiController` subclasses, endpoint routing, Swagger metadata, and DTO responses |
| **[`adding-dto.md`](adding-dto.md)** | Designing Data Transfer Objects with field types, nullability, range enforcement, and Swagger docs |
| **[`adding-service.md`](adding-service.md)** | Crafting domain service use-cases in pure C++, dependency rules, and domain error throwing |
| **[`adding-repository.md`](adding-repository.md)** | Building `BaseRepository` subclasses, ORM `QUERY` templates with bound `:param`s, and schema migrations |
