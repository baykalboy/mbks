// Кастомное правило SonarQube для CVE-2017-9822
// Реализуется как Roslyn Analyzer (DiagnosticAnalyzer).
// Для подключения к SonarQube нужно упаковать в NuGet и добавить
// как external analyzer в sonar-project.properties.

using System.Collections.Immutable;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Diagnostics;

namespace DnnSecurityAnalyzers
{
    [DiagnosticAnalyzer(LanguageNames.CSharp)]
    public class Cve20179822Analyzer : DiagnosticAnalyzer
    {
        // ─── Описание правила ───────────────────────────────────────────────
        public const string DiagnosticId = "DNN0001";

        private static readonly DiagnosticDescriptor Rule = new DiagnosticDescriptor(
            id: DiagnosticId,
            title: "CVE-2017-9822: XmlSerializer создаётся с типом из внешнего источника",
            messageFormat: "new XmlSerializer(Type.GetType({0})) опасен: если аргумент " +
                           "контролируется атакующим, возможно RCE через десериализацию.",
            category: "Security",
            defaultSeverity: DiagnosticSeverity.Error,
            isEnabledByDefault: true,
            description: "CWE-502: Создание XmlSerializer с типом, полученным через " +
                         "Type.GetType() из внешних данных, позволяет атакующему указать " +
                         "произвольный .NET-тип и выполнить код при десериализации."
        );

        public override ImmutableArray<DiagnosticDescriptor> SupportedDiagnostics
            => ImmutableArray.Create(Rule);

        // ─── Регистрируем обработчик узлов AST ─────────────────────────────
        public override void Initialize(AnalysisContext context)
        {
            // Разрешаем конкурентный анализ (производительность)
            context.EnableConcurrentExecution();
            context.ConfigureGeneratedCodeAnalysis(GeneratedCodeAnalysisFlags.None);

            // Анализируем вызовы конструктора ObjectCreationExpression
            context.RegisterSyntaxNodeAction(
                AnalyzeObjectCreation,
                SyntaxKind.ObjectCreationExpression
            );
        }

        // ─── Логика обнаружения ─────────────────────────────────────────────
        private static void AnalyzeObjectCreation(SyntaxNodeAnalysisContext context)
        {
            var creation = (ObjectCreationExpressionSyntax)context.Node;

            // Проверяем: это new XmlSerializer(...) ?
            var typeName = creation.Type.ToString();
            if (!typeName.EndsWith("XmlSerializer"))
                return;

            // Есть ли аргументы?
            if (creation.ArgumentList == null || creation.ArgumentList.Arguments.Count == 0)
                return;

            var firstArg = creation.ArgumentList.Arguments[0].Expression;

            // Проверяем: первый аргумент — вызов Type.GetType(...) ?
            if (firstArg is InvocationExpressionSyntax invocation)
            {
                var memberAccess = invocation.Expression as MemberAccessExpressionSyntax;
                if (memberAccess == null)
                    return;

                bool isTypeGetType =
                    memberAccess.Name.Identifier.Text == "GetType" &&
                    memberAccess.Expression.ToString().EndsWith("Type");

                if (!isTypeGetType)
                    return;

                // Получаем аргумент Type.GetType(X) — это имя типа, переданное снаружи
                var typeArg = invocation.ArgumentList.Arguments.Count > 0
                    ? invocation.ArgumentList.Arguments[0].Expression.ToString()
                    : "?";

                // Проверяем тип аргумента через семантическую модель
                // Если это строковый параметр метода (не константа) — это подозрительно
                var typeInfo = context.SemanticModel.GetTypeInfo(
                    invocation.ArgumentList.Arguments[0].Expression
                );

                bool isStringLiteral =
                    invocation.ArgumentList.Arguments[0].Expression
                    is LiteralExpressionSyntax lit &&
                    lit.IsKind(SyntaxKind.StringLiteralExpression);

                // Строковый литерал — разрешено (тип зафиксирован в коде)
                // Всё остальное (переменная, параметр, результат метода) — опасно
                if (!isStringLiteral)
                {
                    var diagnostic = Diagnostic.Create(
                        Rule,
                        creation.GetLocation(),
                        typeArg
                    );
                    context.ReportDiagnostic(diagnostic);
                }
            }
        }
    }
}
