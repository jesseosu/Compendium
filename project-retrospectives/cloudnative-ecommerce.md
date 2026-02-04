# CloudNative-E-Commerce

> Serverless e-commerce platform on AWS: 7 Lambda microservices, SQS FIFO orders, EventBridge + SNS fan-out, single-table DynamoDB, Bedrock-backed search recommendations. CDK TypeScript. (Repo: `https://github.com/jesseosu/CloudNative-E-Commerce`)

## What it is

A end-to-end serverless commerce platform — React SPA on S3+CloudFront, seven backend Lambda services, all glued together by managed AWS services (SQS, EventBridge, SNS, Cognito). The infrastructure is ~300 lines of CDK TypeScript and the whole stack deploys in one command.

## What I built

- **Frontend** — React 18 SPA, deployed to S3 with CloudFront in front. Cognito JWT for auth.
- **API Gateway** — JWT validation, rate limiting, routing to the right Lambda per resource.
- **7 Lambda services**, one per domain:
  - **Product** — CRUD, batch ops, pagination.
  - **User** — profile + addresses.
  - **Cart** — add/remove with stock validation; DynamoDB TTL on cart items for abandoned-cart cleanup.
  - **Checkout** — creates the order, publishes to SQS FIFO.
  - **Order Processor** — SQS consumer, decrements stock, emits to EventBridge.
  - **Search** — full-text search + Bedrock-backed recommendations with category-based fallback for graceful degradation.
  - **Analytics** — Kinesis stream consumer.
- **DynamoDB** — single-table design, composite partition + sort keys for entity types.
- **SQS FIFO** for order processing — ordering and exactly-once dedup.
- **EventBridge + SNS** for domain-event fan-out — orders publish, multiple subscribers consume independently.
- **X-Ray tracing** across every Lambda.
- **GitHub Actions CI/CD** — lint → test → build → security scan → CDK synth → S3 upload + CloudFront invalidate.
- **Local dev** — `docker-compose` brings up DynamoDB Local with four pre-initialized tables and the frontend in dev mode.

## What I learned

- **Single-table DynamoDB is hard in v1, mostly worth it in v2.** Designing the composite key schema up front feels arbitrary. Six months in, when you want to do a query that wasn't in the original access-pattern list, you'll either pay for a global secondary index or refactor. The discipline of writing down the access patterns first is more valuable than the table count.
- **SQS FIFO trades throughput for correctness.** 300 messages/sec per message group ID, vs effectively unlimited for standard SQS. For order processing this is fine — the group ID can be the customer or the order, parallelism comes from spreading across groups.
- **Bedrock with a fallback path is a production-mindset feature, not an ML feature.** ML calls fail. The category-based fallback ensures the user always gets *some* recommendation; the Bedrock call upgrades it when available.
- **CDK is the right tool for AWS-native serverless.** Composable constructs, real types, refactoring works. Terraform's HCL would be the alternative; CDK won here because the whole thing is TypeScript, which is the same language as the Lambda handlers.
- **DynamoDB TTL is a free cost-saver.** Carts auto-expire after N days; you don't write the cleanup code, you don't run the cleanup job.
- **GitHub Actions per-environment is simple enough.** Branch deploys to dev, main deploys to prod. No external CI server, no Jenkins maintenance.

## Design decisions and tradeoffs

- **SQS FIFO for orders, standard SQS would have been wrong.** Standard SQS is at-least-once with no ordering guarantees; in commerce, "order placed" event delivered out of order or twice is a real bug.
- **Single-table DynamoDB over multiple tables.** Cheaper at scale (one table's RCU/WCU), enables single-request multi-entity queries (customer + their orders in one go). Cost: brittle key schema, harder to reason about.
- **EventBridge + SNS for fan-out instead of just SNS.** EventBridge gives content-based routing rules; SNS doesn't. Cost: extra hop, slightly more cost per event.
- **Bedrock for recommendations with category-based fallback.** Always-degrade-gracefully approach. Cost: two code paths, two failure modes to test.
- **Cognito for auth.** Standard, integrated, solved problem. Cost: lock-in to AWS auth conventions.
- **Per-Lambda IAM roles, least privilege.** More CDK code, more roles to think about, but every Lambda has the minimal permission set. The blast radius of a compromised Lambda is bounded.

## What I'd do differently

- **Add idempotency keys at the API Gateway layer.** SQS FIFO dedup gives me dedup at the queue boundary, but a network retry from the client can still cause duplicate API calls. An idempotency key (UUID v4 from the client, stored in DynamoDB with a short TTL) makes the *whole* request path idempotent.
- **Consider DAX if read patterns concentrate.** Heavy reads on the same product page would benefit from DAX (DynamoDB Accelerator). Wouldn't add it preemptively — needs a measured read concentration first.
- **Cost-trace on real traffic.** Lambda + API Gateway + DynamoDB on-demand is cheap until it isn't. A cost dashboard tagged by service would give me the per-feature unit economics.
- **Synchronous checkout → SQS → async processor is good for happy path; need a saga for rollbacks.** What happens if order processing fails after stock decrement? Today the order is stuck in a bad state. A saga pattern (compensating transactions) would handle this properly.
- **Add load tests.** Lambda concurrency limits, DynamoDB hot partitions, API Gateway throttling — all of these fail in characteristic ways under load. I have no idea where the cliffs are because I haven't tested past tens of requests/sec.
- **Replace polling autoscaling with provisioned concurrency on the latency-sensitive Lambdas.** The product/search Lambdas should not cold-start during a real shopping session.

## Links

- Repo: `https://github.com/jesseosu/CloudNative-E-Commerce`
- Compendium notes:
  - [`../papers/raft-consensus.md`](../papers/raft-consensus.md) — DynamoDB and SQS are built on consensus internally; useful background even though I don't operate the consensus layer.
- External:
  - Alex DeBrie, *The DynamoDB Book* — the canonical reference for single-table design.
  - AWS Well-Architected serverless lens.
  - Sam Newman, *Building Microservices*, 2nd ed. — for the service-boundary thinking.
